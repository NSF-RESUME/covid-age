library(data.table)
library(jsonlite)

run_covid_age <- function(covid_model, par_list = NULL, delete_output = TRUE){
  
  ## covid_model: full path to the executable
  ## par_list: a named list that will be passed to the simulation
  
  if(!is.null(par_list)){
    ## parse parameter list
    alt_input_str <- paste0("'", toJSON(par_list, auto_unbox = TRUE, 
                                        digits = NA), "'")
    
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

# covid_model <- "/home/afadikar/work/projects/git/covid-age/exp/chicago_yr1/model"
# par_list <- list("random_seed" = 23,
#                  "ini_Ki" = 1.5)
# run_covid_age(covid_model, par_list)
