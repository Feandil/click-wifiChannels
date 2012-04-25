#define __STDC_FORMAT_MACROS
#define __STDC_LIMIT_MACROS
#include "basicmta.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <cmath>

const struct option ParamBasicMTA::long_options[] = {
  {"free",        required_argument, 0,  'f' },
  {"err",         required_argument, 0,  'r' },
  {"markov",      required_argument, 0,  'm' },
  {NULL,                          0, 0,   0  }
};

const char * const ParamBasicMTA::needfiles = "MTA needs 3 output files on non human-readable output";

int
ParamBasicMTA::init(const int argc, char **argv, const bool human_readable, const char** err)
{
  int opt, k;
  optind = 1;
  k = 0;
  error_filename = NULL;
  free_filename = NULL;
  markov_filename = NULL;
  while((opt = getopt_long(argc, argv, "k:", long_options, NULL)) != -1) {
    switch(opt) {
      case 'f':
         free_filename = optarg;
        break;
      case 'r':
         error_filename = optarg;
        break;
      case 'm':
         markov_filename = optarg;
        break;
      case 'k':
         k = atoi(optarg);
        break;
      default:
        *err = unknownOption;
        return opt;
    }
  }
  
  if(argc > optind) {
    *err = tooMuchOption;
    return argc;
  }
  
  if ((!human_readable) && ((error_filename == NULL) || (free_filename == NULL) ||(markov_filename == NULL))) {
    *err = needfiles;
    return -1;
  }
  
  mod = new ParamMarckovChain();
  
  if (k == 0) {
    *err = mod->knotset;
    return -1;
  }
  
  success_total = 0;
  error_total = 0;
  current_state = false;
  length = 0;
  length_error = 0;
  second_round = false;
  C = 0;
  mod->init(k, markov_filename);
  return 0;
}

void
ParamBasicMTA::clean(void)
{
  success_length.clear();
  error_length.clear();
  success_length_final.clear();
  error_length_final.clear();
  mod->clean();
}

int
ParamBasicMTA::addChar(const bool input)
{
  if (second_round) {
    if (input == current_state) {
      if (!input) {
        mod->addChar(input);
        ++length_error;
      }
      ++length;
    } else {
      if (current_state) {
        if (length > C) {
          std::map<uint32_t, uint64_t>::iterator temp;
          if (length_error != 0) {
            temp = error_length.find(length_error);
            if (temp == error_length.end()) {
              error_length.insert(std::pair<uint32_t,uint64_t>(length_error, 1));
            } else {
              ++(temp->second);
            }
            ++error_total;
          }
          length_error = 0;
          temp = success_length.find(length);
          if (temp == success_length.end()) {
            success_length.insert(std::pair<uint32_t,uint64_t>(length, 1));
          } else {
            ++(temp->second);
          }
          ++success_total;
        } else {
          uint32_t temp;
          for (temp = 0; temp < length; ++temp) {
            mod->addChar(true);
          }
          length_error += length;
        }
      }
      current_state = input;
      length = 1;
    }
  } else if (input == current_state) {
    ++length;
  } else {
    if (length) {
      std::map<uint32_t, uint64_t>::iterator temp;
      if (!current_state) {
        temp = error_length.find(length);
        if (temp == error_length.end()) {
          error_length.insert(std::pair<uint32_t,uint64_t>(length, 1));
        } else {
          ++(temp->second);
        }
        ++error_total;
      }
    }
    current_state = input;
    length = 1;
  }
  return 0;
}

bool
ParamBasicMTA::nextRound()
{
  /* Consider the last bit */
  addChar(!current_state);
  /* Calculate C */
  double mean = 0, standard_deviation = 0, temp, total = error_total;
  std::map<uint32_t, uint64_t>::const_iterator it;
  for (it = error_length.begin(); it != error_length.end(); ++it) {
    mean += ((double) it->first) * ((double) it->second) / total;
  }
  for (it = error_length.begin(); it != error_length.end(); ++it) {
    temp = ((double) it->first) - mean;
    temp *= temp;
    standard_deviation += temp * ((double) it->second) / total;
  }
  total = mean + std::sqrt(standard_deviation);
  if ((!std::isfinite(total)) || (total >= UINT32_MAX)) {
    std::cerr << "OVERFLOW" << std::endl;
  }
  C = total;
  /* Clean and start new round */
  error_length.clear();
  length = 0;
  length_error = 0;
  second_round = true;
  return true;
}


void
ParamBasicMTA::calculate_values(const uint32_t manx_rand, const std::map<uint32_t, uint64_t> &map, const uint64_t total, std::map<uint32_t, uint32_t> &final)
{
  uint64_t temp_total = 0;
  std::map<uint32_t, uint64_t>::const_iterator it;

  for (it = map.begin(); it != map.end(); ++it) {
    temp_total += (*it).second;
    final.insert(std::pair<uint32_t,uint32_t>(it->first, (uint32_t)(((long double) temp_total) / ((long double) total) * ((long double) manx_rand))));
  }
}

void
ParamBasicMTA::finalize(const uint32_t max_rand)
{
  calculate_values(max_rand, success_length, success_total, success_length_final);
  calculate_values(max_rand, error_length, error_total, error_length_final);
  mod->finalize(max_rand);
}


void
ParamBasicMTA::printBinaryToFile(const std::map<uint32_t, uint32_t> &map, const char* dest)
{
  std::ofstream output;
  output.open(dest);
#define WRITE4(x)  if (output.write((char*)x,4).bad())  std::cerr << "error when writing to output(" << dest << ")" <<std::endl;
  uint32_t size = map.size();
  WRITE4(&size)
   
  std::map<uint32_t, uint32_t>::const_iterator it;
  for (it = map.begin(); it != map.end(); ++it) {
    WRITE4(&it->first);
    WRITE4(&it->second);
  }
  output.close();
}

void
ParamBasicMTA::printBinary(void)
{
  printBinaryToFile(success_length_final, free_filename);
  printBinaryToFile(error_length_final, error_filename);
  mod->printBinary();
}

void
ParamBasicMTA::printHumanToStream(const uint32_t max_rand, const std::map<uint32_t, uint32_t> &map, std::ostream &streamout)
{
  streamout << "(MaxRand: " << max_rand << ")" << std::endl;
  streamout << "CDF size: " << map.size() << std::endl;

  std::map<uint32_t, uint32_t>::const_iterator it;
  for (it = map.begin(); it != map.end(); ++it) {
    streamout << "- " << it->first << ": " << it->second << " (" << ((long double)it->second/((long double) max_rand))*100 << "%)" << std::endl;
  }
}

void
ParamBasicMTA::printHuman(const uint32_t max_rand)
{
  std::ofstream output;
  
  if (free_filename == NULL ) {
    std::cout << "Error-Free-Burst length cdf" << std::endl;
    printHumanToStream(max_rand, success_length_final, std::cout);
    std::cout << std::endl;
  } else {
    output.open(free_filename);
    printHumanToStream(max_rand, success_length_final, output);
    output.close();
  }
  if (error_filename == NULL ) {
    std::cout << "Error-Burst length cdf" << std::endl;
    printHumanToStream(max_rand, error_length_final, std::cout);
    std::cout << std::endl;
  } else {
    output.open(error_filename);
    printHumanToStream(max_rand, error_length_final, output);
    output.close();
  }
  mod->printHuman(max_rand);
}