#include <ostream>

#include "Time_Series.h"
#include "Event_Driven_NUCOVID.h"
#include "json.hpp"

vector<vector<double>> transpose2dVector( vector<vector<double>> vec2d ) {
    vector<vector<double>> tvec2d;

    for (size_t i = 1; i < vec2d.size(); i++) {
        if (not vec2d[i].size() == vec2d[1].size()) {
            cerr << "Vector of vectors not of same size." << endl;
            exit(1);
        }
    }

    for (size_t i = 0; i < vec2d[1].size(); i++) {
        vector<double> v;
        for (size_t j = 0; j < vec2d.size(); j++) { v.push_back(vec2d[j][i]); }    
        tvec2d.push_back(v);
    }

    return tvec2d;
}

void fill_ki_app(const nlohmann::json& params, vector<TimeSeriesAnchorPoint>& Ki_ap) {
    auto data = params["Ki_ap"];
    for (auto d : data) {
        TimeSeriesAnchorPoint pt;
        pt.sim_day = d[0];
        pt.value = d[1];
        Ki_ap.push_back(pt);
    }
}

vector<Node*> initialize_1node(const nlohmann::json& params) {
    vector<Node*> nodes;
    int N = 2500000;
    vector<double> Ki; // S -> E transition rate

    // double ini_Ki = 1.0522;
    double ini_Ki = params["ini_Ki"];

    vector<TimeSeriesAnchorPoint> Ki_ap;
    fill_ki_app(params, Ki_ap);
    
    for (size_t i = 0; i < Ki_ap.size(); i++) { Ki_ap[i].value = Ki_ap[i].value * ini_Ki; }
    Ki = stepwiseTimeSeries(Ki_ap);
    //Ki = linInterpolateTimeSeries(Ki_ap);

    // double Kasymp= 0.4066/3.677037; // E -> I (asymp) rate
    double Kasymp = params["nmrtr_Kasymp"].get<double>()/3.677037;
    double Kpres = 0.5934/3.677037; // E -> I (symp) rate
    // double Kmild = 0.921/3.409656;
    double Kmild = params["nmrtr_Kmild"].get<double>()/3.409656;
    double Kseve = 0.079/3.409656;
    double Khosp = 1.0/4.076704;
    double Kcrit = 1.0/5.592791;
    double Kdeath = 1.0/5.459323;

    vector<double> Krec_asym(400, 1.0/9.0);
    vector<double> Krec_symm(400, 1.0/9.0);
    vector<double> Krec_crit(400, 1.0/9.671261);
    vector<double> Krec_hpc (400, 1.0/2.194643);
    vector<TimeSeriesAnchorPoint> Krh_ap = {
        {0, 1/5.78538},
        {270, 1/4.7},
        {400, 1/4.7}
    };
    vector<double> Krec_hosp = stepwiseTimeSeries(Krh_ap);
    vector<vector<double>> Krec{Krec_asym, Krec_symm, Krec_hosp, Krec_crit, Krec_hpc };
    Krec = transpose2dVector(Krec);
    
    vector<double> Pcrit;
    vector<double> Pcrit_tmp;
    vector<double> Pdeath;
    vector<double> Pdeath_tmp;
    vector<TimeSeriesAnchorPoint> Pcrit_ap = {
        {0, 0.4947      },
        {48, 0.407469   },
        {62, 0.353127   },
        {78, 0.269715   },
        {109, 0.147316  },
        {139, 0.218698  },
        {170, 0.187974  },
        {201, 0.11607   },
        {231, 0.165855  },
        {262, 0.095312  },
        {311, 0.071865  },
        {400, 0.071865  },
    };

    vector<TimeSeriesAnchorPoint> Pdeath_ap = {
        {0, 0.2033      },
        {78, 0.28328    },
        {109, 0.181298  },
        {139, 0.098412  },
        {170, 0.068856  },
        {201, 0.126608  },
        {231, 0.163615  },
        {262, 0.193386  },
        {292, 0.155985  },
        {323, 0.029525  },
        {400, 0.029525  },
    };
    
    Pcrit_tmp = stepwiseTimeSeries(Pcrit_ap);
    Pdeath_tmp = stepwiseTimeSeries(Pdeath_ap);
    for (size_t i = 0; i < Pcrit_tmp.size(); i++) {
        Pcrit.push_back(Pcrit_tmp[i] + Pdeath_tmp[i]);
        Pdeath.push_back(Pdeath_tmp[i] / (Pcrit_tmp[i] + Pdeath_tmp[i]));
    }

    vector<double> Pdet_asym;
    vector<double> Pdet_pres;
    vector<double> Pdet_symm;
    vector<TimeSeriesAnchorPoint> Pdsm_ap = {
        {0, 0.000630373},
        {48, 0.03520381},
        {78, 0.08413338},
        {109, 0.1474772},
        {139, 0.1528583},
        {170, 0.1064565},
        {201, 0.1514133},
        {231, 0.1608695},
        {262, 0.4160216},
        {400, 0.4160216}
    };
    Pdet_symm = stepwiseTimeSeries(Pdsm_ap);

    vector<double> Pdet_syms;
    vector<TimeSeriesAnchorPoint> Pdss_ap = {
        {0, 0.009849325},
        {31, 0.1456243 },
        {48, 0.5841068 },
        {78, 0.6877389 },
        {109, 0.9820229},
        {139, 0.5239712},
        {170, 0.5520378},
        {201, 0.7033732},
        {231, 0.881767 },
        {400, 0.881767 }
    };
    Pdet_syms = stepwiseTimeSeries(Pdss_ap);

    for (size_t i = 0; i < Pdet_symm.size(); i++) {
        Pdet_asym.push_back(Pdet_symm[i]/6);
        Pdet_pres.push_back(Pdet_symm[i]/6);
    }

    vector<vector<double>> Pdetect{Pdet_asym, Pdet_pres, Pdet_symm, Pdet_syms};
    Pdetect = transpose2dVector(Pdetect);

    // double frac_infectiousness_As = 0.8;
    double frac_infectiousness_As = params["frac_infectiousness_As"];
    // double frac_infectiousness_det = 0.00733;
    double frac_infectiousness_det = params["frac_infectiousness_det"];
    vector<double> time_to_detect = {1.904861, 7, 2};

    Node* n1 = new  Node(0, N, Ki, Kasymp, Kpres, Kmild, Kseve, Khosp, Kcrit,
                         Kdeath, Krec, Pcrit, Pdeath, Pdetect,
                         frac_infectiousness_As, frac_infectiousness_det, time_to_detect);
    nodes.push_back(n1);
    return(nodes);
}

void runsim (const nlohmann::json& params) {
    cout << "Running Sim"<< endl;
    if (params["print_params"]) {
        for (auto& el : params.items()) {
            std::cout << el.key() << " : " << el.value() << std::endl;
        }
    }

    vector<string> out_buffer;
    vector<Node*> nodes = initialize_1node(params);
    vector<vector<double>> infection_matrix;
    for(int i = 0; i < 1; i++) {
        vector<double> inf_prob;
        inf_prob.push_back(1);
        infection_matrix.push_back(inf_prob);
    }

    Event_Driven_NUCOVID sim(nodes, infection_matrix);

    std::map<int, int> seeds;
    auto data = params["random_seeds"];
    for (auto d : data) {
        seeds.emplace(d[0], d[1]);
    }
    auto iter = seeds.find(0);
    if (iter == seeds.end()) {
        std::cout << "Aborting. Please provide an initial time 0 random seed" << std::endl;
        return;
    }
    // std::cout << "Setting seed to " << iter->second << std::endl;
    sim.rng.seed(iter->second);
    sim.Now = 9;
    sim.rand_infect(10, nodes[0]);//*2
    double duration = params["duration"].get<double>();
    out_buffer = sim.run_simulation(duration, seeds, false);

    string out_fname = params["output_directory"].get<string>()+ "/" +
        params["output_filename"].get<string>();
    write_buffer(out_buffer, out_fname, true);

    return;
}

void usage() {
    std::cerr << "usage: model [json map formatted parameters]" << std::endl;
}

int parse_params(nlohmann::json& params, const std::string& cl_params) {
    nlohmann::json j2 = nlohmann::json::parse(cl_params);
    for (auto& el : j2.items()) {
        string key = el.key();
        if (!params.contains(key)) {
            std::cerr << "Invalid parameter: " + key << std::endl;
            return -1;
        }
        params[key] = el.value();
        // std::cout << el.key() << " : " << el.value() << "\n";
    }
    return 0;
}

void load_default_params(nlohmann::json& params) {
    // DEFAULT PARAMETERS
    params["output_directory"] = "./";
    params["output_filename"] = "daily_output.txt";
    params["nmrtr_Kasymp"] = 0.4066;
    params["nmrtr_Kmild"] = 0.921;
    params["ini_Ki"] = 1.0522;
    params["frac_infectiousness_As"] = 0.8;
    params["frac_infectiousness_det"] =  0.00733;
    params["duration"] = 371; 
    params["print_params"] = false;
    params["Ki_ap"] =  {
        {0, 1.0     },
        {28, 0.6263 },
        {33, 0.3526 },
        {37, 0.09   },
        {68, 0.07   },
        {98, 0.07   },
        {129, 0.11  },
        {163, 0.11  },
        {217, 0.13  },
        {237, 0.198 },
        {272, 0.115 },
        {311, 0.117 },
        {342, 0.1156},
        {368, 0.1223},
        {400, 0.1223}
    };
    params["random_seeds"] = {{0, 42}};
}

int main(int argc, char* argv[]) { 
    if (argc > 2) {
        usage();
        return -1;
    } else {
        nlohmann::json params;
        load_default_params(params);
        if (argc == 2) {
            int ret_val = parse_params(params, argv[1]);
            if (ret_val != 0) return -1;
        }
        runsim(params);
    }
    return 0;
}
