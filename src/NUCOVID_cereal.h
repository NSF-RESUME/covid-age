#ifndef ED_NUCOVID_H
#define ED_NUCOVID_H

#include <stdlib.h>
#include <vector>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <queue>
#include <sstream>
#include "Utility.h"
#include <climits>
#include "sys/stat.h"
#include <cereal/archives/binary.hpp>
#include <cereal/types/vector.hpp>
#include <cereal/types/queue.hpp>
#include <cereal/types/memory.hpp>

using namespace std;

double inv_adj_inv (double a, double b) { return 1/(1/a - b); }

typedef enum {
    SUSCEPTIBLE, 
    EXPOSED, 
    ASYMPTOMATIC, 
    PRESYMPTOMATIC, 
    SYMPTOMATIC_MILD,
    SYMPTOMATIC_SEVERE,
    HOSPITALIZED,
    HOSPITALIZED_CRIT,
    CRITICAL,
    DEATH,
    RESISTANT, 
    STATE_SIZE // STATE_SIZE must be last
} stateType;

typedef enum {
    PRE, ASY, SYMM, SYMS, HOS, CRI, HPC, DEA, RECA, RECM, RECH, RECC, CON, IMM
} eventType;

class Node {
    public:
        int id;
        int N;                      // population size
        vector<double> Ki;          // param for exponential exposed duration
        double Kasym;               // param for exponential time to infectious (asymptomatic)
        double Kpres;               // param for exponential time to infectious (presymptomatic)
        double Kmild;               // param for exponential time to mild from presymp
        double Ksevere;             // param for exponential time to severe from presymp 
        double Khosp;               // param for exponential time to hospitalized from severe 
        double Kcrit;               // param for exponential time to critical from hospitalized 
        double Kdeath;              // param for exponential time to death from critical
        vector<vector<double>> Krec;// param for exponential time to recovery (A, Sm, H, C, HPC)
        vector<double> Pcrit;       // probability of critical given hospitalized
        vector<double> Pdeath;      // probability of death given critical
        vector<vector<double>> Pdetect; // probability of detection (A, P, Sm, Ss)
        double frac_infectiousness_As;  // infectiousness multiplier for As
        double frac_infectiousness_det; // infectiousness multiplier for detected
        vector<int> state_counts;   // S, E, I, R counts
        vector<double> time_to_detect;
        size_t cumu_symptomatic;
        size_t cumu_admission;
        size_t introduced;

        // constructor
        Node( int ii, int n, vector<double> ki, double ka, double kp, double km, double ks, 
              double kh, double kc, double kd, vector<vector<double>> kr,
              vector<double> pc, vector<double> pd, vector<vector<double>> pdets,
              double fia, double fid, vector<double> t2det) {

            id = ii;
            N = n;
            Ki = ki;
            Kasym = ka;
            Kpres = kp; 
            Kmild = km;
            Ksevere = ks;
            Khosp = kh;
            Kcrit = kc;
            Kdeath = kd;
            Krec = kr;
            Pcrit = pc;
            Pdeath = pd;
            Pdetect = pdets;
            cumu_symptomatic = 0;
            cumu_admission = 0;
            introduced = 0;

            frac_infectiousness_As = fia;
            frac_infectiousness_det = fid;
            time_to_detect = t2det;
        }

        Node() {};

        void reset() {
            state_counts.clear();
            state_counts.resize(STATE_SIZE, 0);
            state_counts[SUSCEPTIBLE] = N;
        }

        double get_Ki(int day) { return day < Ki.size() ? Ki[day] : Ki[Ki.size() - 1]; }
        double get_Pdet(int day, int ind) { return day < Pdetect.size() ? Pdetect[day][ind] : Pdetect[Pdetect.size() - 1][ind]; }
        double get_Pcrit(int day) { return day < Pcrit.size() ? Pcrit[day] : Pcrit[Pcrit.size() - 1]; }
        double get_Pdeath(int day) { return day < Pdeath.size() ? Pdeath[day] : Pdeath[Pdeath.size() - 1]; }
        double get_Krec(int day, int ind) { return day < Krec.size() ? Krec[day][ind] : Krec[Krec.size() - 1][ind]; }

        template<class Archive>
        void serialize(Archive & archive) {
            archive( id, N, Ki, Kasym, Kpres, Kmild, Ksevere, Khosp, Kcrit, Kdeath, Krec,
                     Pcrit, Pdeath, Pdetect, frac_infectiousness_As, frac_infectiousness_det,
                     state_counts, time_to_detect, cumu_symptomatic, cumu_admission, introduced );
        }
};

class Event {
    public:
        double time;
        eventType type;
        shared_ptr<Node> source_node;
        shared_ptr<Node> target_node;
        bool detect;
        Event() {};
        Event(const Event& o) {  time=o.time; type=o.type; source_node=o.source_node; target_node=o.target_node; detect=o.detect;}
        Event(double t, eventType e, shared_ptr<Node> sn, shared_ptr<Node> tn, bool det) {time=t; type=e; source_node=sn; target_node=tn; detect=det;}
        Event& operator=(const Event& o) { time=o.time; type=o.type; source_node=o.source_node; target_node=o.target_node; detect=o.detect; return *this; }

        template<class Archive>
        void serialize(Archive & archive) {
            archive( time, type, source_node, target_node, detect );
        }
};

class compTime {
    public:
        bool operator() (const Event* lhs, const Event* rhs) const {
            return (lhs->time>rhs->time);
        }

        bool operator() (const Event& lhs, const Event& rhs) const {
            return (lhs.time>rhs.time);
        }
};


namespace cereal {

// External serialization functions should be placed either
// in the same namespace as the types they serialize or in the
// cereal namespace so that the compiler can find them properly.

template<class Archive>
void save(Archive& archive, std::mt19937 const & rng) {
    std::stringstream ss;
    ss << rng;
    archive(ss.str());
}

template<class Archive>
void load(Archive& archive, std::mt19937 & rng) {
    std::string s;
    archive(s);
    std::istringstream is(s);
    is >> rng;
}

}


class CachedBitGenerator {
    
    std::vector<mt19937::result_type> cache;
    size_t idx;
    

public:
    size_t calls;

    typedef mt19937::result_type result_type; 

    CachedBitGenerator(mt19937& rng, size_t cache_size) : cache{}, idx{0}, calls{0} {
        for (size_t i = 0; i < cache_size; ++i) {
            cache.push_back(rng());
        }
    }

    static constexpr result_type min() {
        return mt19937::min();
    }

    static constexpr result_type max() {
        return mt19937::max();
    }

    result_type operator()() {
        if (idx == cache.size()) {
            idx = 0;
            std::cout << "WARNING: CachedBitGenerator exhausted. Choose a larger cache size" << std::endl;
        }
        result_type val = cache[idx];
        ++idx;
        ++calls;
        return val;
    }
};

class Event_Driven_NUCOVID {
    public:
        vector<shared_ptr<Node>> nodes;
        vector<vector<double>> infection_matrix;
        priority_queue<Event, vector<Event>, compTime > EventQ; // event queue
        double Now; // Current "time" in simulation
        double offset;                
        mt19937 rng;              // RNG
        
        Event_Driven_NUCOVID () {};
        Event_Driven_NUCOVID (vector<shared_ptr<Node>> ns, vector<vector<double>> mat) {
            nodes = ns;
            infection_matrix = mat;
            
            // integrity check nodes vs infection_matrix
            assert(mat.size() == ns.size());
            for (size_t i = 0; i < mat.size(); i++) {
                assert(mat[i].size() == ns.size());
            }
            reset();
        }

        void print_state (vector<string>* out_buffer, int day, bool print) {
            for (size_t i = 0; i < nodes.size(); i++) {
                const shared_ptr<Node> n = nodes[i];
                stringstream ss;
                ss << setprecision(5) << n->id << "\t"   
                                      << day << "\t"  << n->Ki[day] << "\t" 
                                      << n->state_counts[SUSCEPTIBLE] << "\t" 
                                      << n->state_counts[EXPOSED] << "\t" 
                                      << n->state_counts[ASYMPTOMATIC] + n->state_counts[PRESYMPTOMATIC]<< "\t" 
                                      << n->state_counts[SYMPTOMATIC_MILD] + n->state_counts[SYMPTOMATIC_SEVERE] << "\t"
                                      << n->state_counts[HOSPITALIZED] + n->state_counts[HOSPITALIZED_CRIT]<< "\t"
                                      << n->state_counts[CRITICAL] << "\t"
                                      << n->state_counts[DEATH] << "\t"
                                      << n->state_counts[RESISTANT] << "\t"
                                      << n->cumu_symptomatic << "\t"
                                      << n->cumu_admission << "\t"
                                      << n->introduced;

                out_buffer->push_back(ss.str());
                if (print) {cout << ss.str() << endl;}
            }

        }

        double check_next_event_time() {
                if ( EventQ.empty() ) return (-1);
                return ( EventQ.top().time );
        }

        vector<string> run_simulation(double duration, std::map<double, int> seeds, bool print) {
            double start_time = Now;
            double intpart;
            int day;

            vector<string>* out_buffer = new vector<string>;
            string header = "node\ttime\tKi\tS\tE\tAP\tSYM\tHOS\tCRIT\tDEA\tR\tcumu_sym\tcumu_adm\tintroduced";
            cout << setprecision(3) << fixed;
            out_buffer->push_back(header);
            if (print) {cout << header << endl;}

            if ( modf(start_time, &intpart) == 0 ) {
                day = (int) start_time;
            } else {
                day = ceil(start_time);
            }

            std::cout << "start_time, duration, offset: " << start_time << ", " << duration << ", " << offset << std::endl;
            double next_event_time = check_next_event_time();
            // std::cout << "start_time, duration: " << start_time << ", " << duration << std::endl;
            // std::cout << "1 Next Evt Time: " << next_event_time << std::endl;
            while ( (next_event_time != -1) and (next_event_time < start_time + duration + offset) ) {
                if (next_event_time > day) {
                    auto iter = seeds.find(static_cast<double>(day));
                    if (iter != seeds.end()) {
                        int seed = iter->second;
                        // std::cout << "Updating seed at day " << day << " to " << seed << std::endl;
                        rng.seed(seed);
                    }
                    print_state(out_buffer, day, print);
                    day++;
                }

                next_event();
                next_event_time = check_next_event_time();
                // std::cout << "Next Evt Time: " << next_event_time << std::endl;
                continue;
            }
            // std::cout << "2 Next Evt Time: " << next_event_time << std::endl;
            // offset = duration - Now
            offset = (start_time + duration + offset) - Now;
            std::cout << "duration, now, offset: " << duration << ", " << Now << ", " << offset << std::endl;

            // non-continued sims are set to start at 9, so we need
            // to account for that.
            // if (start_time == 9.0) offset += 9.0;
            // std::cout << duration << " " << Now << std::endl;
            print_state(out_buffer, day, print);

            return (*out_buffer);
        }

        //Epidemic size not used for now
        //int current_epidemic_size() {
        //    return state_counts[EXPOSED] + state_counts[ASYMPTOMATIC] + state_counts[PRESYMPTOMATIC] + 
        //           state_counts[SYMPTOMATIC_MILD] + state_counts[SYMPTOMATIC_SEVERE] +
        //           state_counts[HOSPITALIZED] + state_counts[CRITICAL];
        //}

        void reset() {
            offset = 0.0;
            Now = 0.0;
            for (size_t i = 0; i < nodes.size(); i++) {
                nodes[i]->reset();
            }
            EventQ = priority_queue<Event, vector<Event>, compTime > ();
        }

        void rand_infect(int k, shared_ptr<Node> n) {   // randomly infect k people
            for (unsigned int i = 0; i < k; i++) {
                CachedBitGenerator cbg(rng, 100);
                infect(n, cbg);
                //import_As(n);
            }
        }
        
        double get_detection_modifier(double p, double r) {return p * r + (1.0 - p);}

        template<typename RNG_T>
        size_t get_infection_node_id(size_t nid, RNG_T& cbg) {
            double total_weight = 0.0;
            size_t chosen;
            for (size_t i = 0; i < infection_matrix.size(); i++) { total_weight += infection_matrix[nid][i]; }
            double r = rand_uniform(0, total_weight, &cbg);
            for (size_t i = 0; i < infection_matrix.size(); i++) { 
                if (r < infection_matrix[nid][i]) {
                    chosen = i;
                    break;
                } else {
                    r -= infection_matrix[nid][i]; 
                }
            }

            return chosen;
        }

        template<typename RNG_T>
        void import_As(shared_ptr<Node> n, RNG_T& cbg) {
            n->state_counts[SUSCEPTIBLE]--;  // decrement susceptible groupjj
            n->state_counts[ASYMPTOMATIC]++;      // increment exposed group

            double Ti;
            double Tr;
            vector<double> Times;
            vector<double> Ki_modifier;
            bool det_flag = false;

            // ASYMPTOMATIC PATH
            // time to become infectious
            Ti = Now;
            if (not det_flag) {
                det_flag = rand_uniform(0, 1, &cbg) < n->get_Pdet((int) Ti, 0) ? true : false;
            }
            Times.push_back(Ti);
            Ki_modifier.push_back(det_flag ? n->frac_infectiousness_det * n->frac_infectiousness_As : n->frac_infectiousness_As);
 
            // time to recovery
            Tr = rand_exp(n->get_Krec((int) Ti, 0), &cbg) + Ti;
            add_event(Tr, RECA, n, n, det_flag);

            // time to next contact
            int bin = 0;
            double Tc = rand_exp(n->get_Ki((int) Ti) * Ki_modifier[bin], &cbg) + Ti;
            while ( Tc < Tr ) {     // does contact occur before recovery?
                // decide which node to infect
                size_t infect_node_id = get_infection_node_id(n->id, cbg);
                add_event(Tc, CON, n, nodes[infect_node_id], det_flag); // potential transmission event
                while (bin < Times.size() - 1 and Times[bin+1] < Tc) {bin++;} // update bin if necessary
                Tc += rand_exp(n->get_Ki((int) Tc) * Ki_modifier[bin], &cbg);
            }
        }

        template<typename RNG_T>
        void infect(shared_ptr<Node> n, RNG_T& cbg) {
            assert(n->state_counts[SUSCEPTIBLE] > 0);
            n->state_counts[SUSCEPTIBLE]--;  // decrement susceptible groupjj
            n->state_counts[EXPOSED]++;      // increment exposed group

            // time to become infectious (duel between presymp and asymp)
            double Tpres = rand_exp(n->Kpres, &cbg) + Now;
            double Tasym = rand_exp(n->Kasym, &cbg) + Now;
            double Ti;
            double Tsym;
            double Tr;
            double Th = 0;
            double Tcr = -1;
            double Thc = 0;
            double Td = 0;
            vector<double> Times;
            vector<double> Ki_modifier;
            bool det_flag = false;

            // Start differentiating the two paths
            if (Tpres < Tasym) {
                // SYMPTOMATIC PATH
                // time to become infectious
                
                // Pre-symptomatic phase (no pre-detection here)
                Ti = Tpres;
                if (not det_flag) det_flag = rand_uniform(0, 1, &cbg) < n->get_Pdet((int) Ti, 1);
                add_event(Tpres, PRE, n, n, det_flag);
                Times.push_back(Ti);
                Ki_modifier.push_back(det_flag ? n->frac_infectiousness_det : 1);

                double Tmild = rand_exp(n->Kmild, &cbg) + Tpres;
                double Tsevere = rand_exp(n->Ksevere, &cbg) + Tpres;

                if (Tmild < Tsevere) {
                    // Mild SYMPTOMATIC PATH
                    // pre-detection phase
                    Tsym = Tmild;
                    add_event(Tsym, SYMM, n, n, det_flag);
                    Times.push_back(Tsym);
                    Ki_modifier.push_back(det_flag ? n->frac_infectiousness_det : 1);

                    // detection phase
                    double Tdet = rand_exp(1/n->time_to_detect[1], &cbg) + Tsym;
                    if (not det_flag) det_flag = rand_uniform(0, 1, &cbg) < n->get_Pdet((int) Tsym, 2);
                    Times.push_back(Tdet);
                    Ki_modifier.push_back(det_flag ? n->frac_infectiousness_det : 1);

                    Tr = rand_exp(inv_adj_inv(n->get_Krec((int) Ti, 1), n->time_to_detect[1]), &cbg) + Tdet;
                    add_event(Tr, RECM, n, n, det_flag);
                } else {
                    // Severe SYMPTOMATIC PATH
                    // pre-detection phase
                    Tsym = Tsevere;
                    add_event(Tsym, SYMS, n, n, det_flag);
                    Times.push_back(Tsym);
                    Ki_modifier.push_back(det_flag ? n->frac_infectiousness_det : 1);

                    // detection phase
                    double Tdet = rand_exp(1/n->time_to_detect[2], &cbg) + Tsym;
                    if (not det_flag) det_flag = rand_uniform(0, 1, &cbg) < n->get_Pdet((int) Tsym, 3);
                    Times.push_back(Tdet);
                    Ki_modifier.push_back(det_flag ? n->frac_infectiousness_det : 1);

                    Th = rand_exp(inv_adj_inv(n->Khosp, n->time_to_detect[2]), &cbg) + Tdet;
                    add_event(Th, HOS, n, n, det_flag);
                    Times.push_back(Th);
                    Ki_modifier.push_back(det_flag ? n->frac_infectiousness_det : 1);

                    if (rand_uniform(0, 1, &cbg) > n->get_Pcrit((int) Th)) {
                        // Hospitalized and recovered
                        Tr = rand_exp(n->get_Krec((int) Th, 2), &cbg) + Th;
                        add_event(Tr, RECH, n, n, det_flag);
                    } else {
                        // Hospitalized and become critical
                        Tcr = rand_exp(n->Kcrit, &cbg) + Th;
                        add_event(Tcr, CRI, n, n, det_flag);

                        if (rand_uniform(0, 1, &cbg) > n->get_Pdeath((int) Tcr)) {
                            // Critical and recovered
                            Thc = rand_exp(n->get_Krec((int) Tcr, 3), &cbg) + Tcr;
                            add_event(Thc, HPC, n, n, det_flag);

                            Tr = rand_exp(n->get_Krec((int) Thc, 4), &cbg) + Thc;
                            add_event(Tr, RECC, n, n, det_flag);
                        } else {
                            // Critical and die
                            Tr = rand_exp(n->Kdeath, &cbg) + Tcr;
                            add_event(Tr, DEA, n, n, det_flag);
                        } 
                    }

                }

            } else {
                // ASYMPTOMATIC PATH
                // time to become infectious
                Ti = Tasym;
                
                // pre-detection phase
                add_event(Tasym, ASY, n, n, det_flag);
                Times.push_back(Ti);
                Ki_modifier.push_back(n->frac_infectiousness_As);
                
                // detection phase
                double Tdet = rand_exp(1/n->time_to_detect[0], &cbg) + Ti;
                if (not det_flag) det_flag = rand_uniform(0, 1, &cbg) < n->get_Pdet((int) Ti, 0);
                Times.push_back(Tdet);
                Ki_modifier.push_back(det_flag ? n->frac_infectiousness_det : n->frac_infectiousness_As);
                //Ki_modifier.push_back(det_flag ? n->frac_infectiousness_det * n->frac_infectiousness_As : n->frac_infectiousness_As);

                // time to recovery
                Tr = rand_exp(inv_adj_inv(n->get_Krec((int) Ti, 0), n->time_to_detect[0]), &cbg) + Tdet;
                add_event(Tr, RECA, n, n, det_flag);
            }
            
            // time to next contact
            int bin = 0;
            double Tc = rand_exp(n->get_Ki((int) Ti) * Ki_modifier[bin], &cbg) + Ti;
            while ( Tc < Tr ) {     // does contact occur before recovery?
                // decide which node to infect
                size_t infect_node_id = get_infection_node_id(n->id, cbg);
                add_event(Tc, CON, n, nodes[infect_node_id], det_flag); // potential transmission event
                while (bin < Times.size() - 1 and Times[bin+1] < Tc) {bin++;} // update bin if necessary
                Tc += rand_exp(n->get_Ki((int) Tc) * Ki_modifier[bin], &cbg);
            }
            
            // time to become susceptible again (not used for now)
            //double Ts = Tr + immunity_duration; 
            //add_event(Ts, IMM);
            //return;
        }

        int next_event() {
            if ( EventQ.empty() ) return 0;
            Event event = EventQ.top(); // get the element
            EventQ.pop();               // remove from Q

            Now = event.time;           // advance time
            //cerr << "Time: " << Now << " Event: " << event.type << endl;
            switch(event.type) {
                case PRE:
                    event.source_node->state_counts[EXPOSED]--;      // decrement exposed class
                    event.target_node->state_counts[PRESYMPTOMATIC]++;   // increment symptomatic class
                    break;
                case ASY:
                    event.source_node->state_counts[EXPOSED]--;      // decrement exposed class
                    event.target_node->state_counts[ASYMPTOMATIC]++;   // increment asymptomatic class
                    break;
                case SYMM:
                    event.source_node->state_counts[PRESYMPTOMATIC]--;      // decrement exposed class
                    event.target_node->state_counts[SYMPTOMATIC_MILD]++;   // increment symptomatic class
                    event.target_node->cumu_symptomatic++;
                    break;
                case SYMS:
                    event.source_node->state_counts[PRESYMPTOMATIC]--;      // decrement exposed class
                    event.target_node->state_counts[SYMPTOMATIC_SEVERE]++;   // increment symptomatic class
                    //event.target_node->cumu_symptomatic++;
                    break;
                case HOS:
                    event.source_node->state_counts[SYMPTOMATIC_SEVERE]--;      // decrement exposed class
                    event.target_node->state_counts[HOSPITALIZED]++;   // increment symptomatic class
                    event.target_node->cumu_admission++;
                    break;
                case CRI:
                    event.source_node->state_counts[HOSPITALIZED]--;      // decrement exposed class
                    event.target_node->state_counts[CRITICAL]++;   // increment symptomatic class
                    break;
                case HPC:
                    event.source_node->state_counts[CRITICAL]--;      // decrement exposed class
                    event.target_node->state_counts[HOSPITALIZED_CRIT]++;   // increment symptomatic class
                    break;
                case DEA:
                    event.source_node->state_counts[CRITICAL]--;      // decrement exposed class
                    event.target_node->state_counts[DEATH]++;   // increment symptomatic class
                    break;
                case RECA:
                    event.source_node->state_counts[ASYMPTOMATIC]--;      // decrement asymptomatic class
                    event.target_node->state_counts[RESISTANT]++;   // increment recovery class
                    break;
                case RECM:
                    event.source_node->state_counts[SYMPTOMATIC_MILD]--;      // decrement symptomatic class
                    event.target_node->state_counts[RESISTANT]++;   // increment recovery class
                    break;
                case RECH:
                    event.source_node->state_counts[HOSPITALIZED]--;      // decrement symptomatic class
                    event.target_node->state_counts[RESISTANT]++;   // increment recovery class
                    break;
                case RECC:
                    event.source_node->state_counts[HOSPITALIZED_CRIT]--;      // decrement symptomatic class
                    event.target_node->state_counts[RESISTANT]++;   // increment recovery class
                    break;
                case IMM:
                    event.source_node->state_counts[RESISTANT]--;      // decrement recovery class
                    event.target_node->state_counts[SUSCEPTIBLE]++;   // increment susceptible class
                    break;
                case CON:
                    {
                        CachedBitGenerator cbg(rng, 250);
                        // std::cout << Now << ": " << rng() << std::endl;
                        // const int rand_contact = rand_uniform_int(0, event.target_node->N, &rng);
                        const int rand_contact = rand_uniform_int(0, event.target_node->N, &cbg);
                        if (rand_contact < event.target_node->state_counts[SUSCEPTIBLE]) {
                            if (not event.target_node->id == event.source_node->id) event.target_node->introduced++;
                            infect(event.target_node, cbg);
                        }

                        // std::cout << Now << ": " << cbg.calls << std::endl;
                    }
                    break;
                default:    
                    cerr << "Unknown event type encountered in simulator: " << event.type << "\nQuitting.\n";
            }
            return 1;
        }

        void add_event( double time, eventType type, shared_ptr<Node> sn, shared_ptr<Node> tn, bool detect) {
            // std::cout << "evt: " << time << std::endl;
            EventQ.push( Event(time,type,sn,tn,detect) );
            return;
        }

        template<class Archive>
        void serialize(Archive & archive) {
            archive( nodes, infection_matrix, EventQ, Now, offset );
            archive(rng);
        }

};

bool fileExists(const std::string& filename) {
    struct stat buf;
    return stat(filename.c_str(), &buf) != -1;
}

void write_buffer(vector<string>& buffer, string filename, bool overwrite) {
    string all_output;
    for (const auto &line : buffer) all_output += (line + "\n");

    if (fileExists(filename) and not overwrite) {
        cerr << "WARNING: Daily output file already exists: " << filename << endl << "WARNING: Aborting write.\n";
        return;
    }

    ofstream file;
    file.open(filename);

    if (file.is_open()) {  // TODO - add this check everywhere a file is opened
        file << all_output;
        file.close();
    } else {
        cerr << "ERROR: Could not open daily buffer file for output: " << filename << endl;
        exit(-842);
    }
}

#endif
