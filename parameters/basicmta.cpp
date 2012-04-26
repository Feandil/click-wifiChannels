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
  
  markov = new ParamMarckovChain();
  onoff = new ParamBasicOnOff();
  
  if (k == 0) {
    *err = markov->knotset;
    return -1;
  }
  
  current_state = false;
  length = 0;
  length_error = 0;
  second_round = false;
  C = 0;
  markov->init(k, markov_filename);
  onoff->init(error_filename, free_filename);
  return 0;
}

void
ParamBasicMTA::clean(void)
{
  onoff->clean();
  markov->clean();
}

int
ParamBasicMTA::addChar(const bool input)
{
  if (second_round) {
    if (input == current_state) {
      if (!input) {
        markov->addChar(input);
      }
      ++length;
    } else {
      if (current_state) {
        if (length > C) {
          if (length_error != 0) {
            onoff->addChars(false, length_error);
          }
          length_error = 0;
          onoff->addChars(true, length);
        } else {
          uint32_t temp;
          for (temp = 0; temp < length; ++temp) {
            markov->addChar(true);
          }
          length_error += length;
        }
        markov->addChar(input);
      } else {
        length_error += length;
      }
      length = 1;
    }
  } else {
    onoff->addChar(input);
  }
  current_state = input;
  return 0;
}

bool
ParamBasicMTA::nextRound()
{
  /* Consider the last bit */
  onoff->addChar(!current_state);
  /* Calculate C */
  double mean = 0, standard_deviation = 0, temp, total = onoff->getRawErrorBurstNumber();;
  const std::map<uint32_t, uint64_t>* errors = onoff->getRawErrorBurstLengthCDF();
  std::map<uint32_t, uint64_t>::const_iterator it;
  for (it = errors->begin(); it != errors->end(); ++it) {
    mean += ((double) it->first) * ((double) it->second) / total;
  }
  for (it = errors->begin(); it != errors->end(); ++it) {
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
  onoff->clean();
  onoff->init(NULL, NULL);
  length = 0;
  length_error = 0;
  second_round = true;
  return true;
}

void
ParamBasicMTA::finalize(const uint32_t max_rand)
{
  if (current_state) {
    if (length > C) {
      if (length_error != 0) {
        onoff->addChars(false, length_error);
      }
      onoff->addChars(true, length);
    } else {
      uint32_t temp;
      for (temp = 0; temp < length; ++temp) {
        markov->addChar(true);
      }
      length_error += length;
      onoff->addChars(false, length_error);
    }
  } else {
    length_error += length;
    onoff->addChars(false, length_error);
  }
  onoff->finalize(max_rand);
  markov->finalize(max_rand);
}

void
ParamBasicMTA::printBinary(void)
{
  onoff->printBinary();
  markov->printBinary();
}

void
ParamBasicMTA::printHuman(const uint32_t max_rand)
{
  onoff->printHuman(max_rand);
  markov->printHuman(max_rand);
}
