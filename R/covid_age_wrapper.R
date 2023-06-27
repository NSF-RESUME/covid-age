library(data.table)
library(jsonlite)

run_covid_age <- function(covid_model, par_list = NULL, delete_output = TRUE){
  
  ## covid_model: full path to the executable
  ## par_list: a named list that will be passed to the simulation
  
  if(!is.null(par_list)){
    ## parse parameter list
    alt_input_str <- paste0("'", toJSON(par_list, auto_unbox = TRUE, 
                                        digits = NA), "'")
    
    par_names <- names(par_list)
    
    ## check if output_file and output_directory are present
    if("output_directory" %in% par_names){
      if("output_filename" %in% par_names){
        out_file <- paste0(par_list["output_directory"], "/", par_list["output_filename"])
      } else {
        out_file <- paste0(par_list["output_directory"], "/", "daily_output.txt")
      }
    } else {
      if("output_filename" %in% par_names){
        out_file <- paste0(getwd(), "/", par_list["output_filename"])
      } else {
        out_file <- paste0(getwd(), "/", "daily_output.txt")
      }
    }
  }
  
  x <- paste(covid_model, alt_input_str)
  # cat(x)
  system(x)
  # cat(out_file)
  ## read the  output
  dt <- fread(out_file)
  
  if(delete_output)  unlink(out_file)
  return(dt)
}


# Example of running covid-age simulation with Ki_ap input.
#
# Ki_ap is a step function like time series and 
# should be passed as a matrix to the input par_list.
# The first column of the Ki_ap matrix contains the time indices where 
# the jump occurs in the step function and the second column should have 
# the corresponding transmissibility values. In the example below, 
# Ki_ap time series has value 1 between time 0 and 27, 0.7 between 28 and 59, 
# 1 between 60 and simulation end time.
#
#

# covid_model <- "/home/afadikar/work/projects/git/covid-age/exp/chicago_yr1/model"
# par_list <- list("random_seed" = 23,
#                  "ini_Ki" = 0.97,
#                  "Ki_ap" = rbind(c(0, 1), c(28, 0.7), c(60, 1)),
#                  "output_directory" = "/home/afadikar/temp",
#                  "output_filename" = paste0("out_", sample(1e9, 1), ".out"),
#                  "duration" = 100)
# run_covid_age(covid_model, par_list)
