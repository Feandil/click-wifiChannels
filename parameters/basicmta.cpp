/** @file basicmta.cpp Implementation of the Basic MTA parameter generation module */

#define __STDC_LIMIT_MACROS
#include "basicmta.h"
#include <assert.h>
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

  /* Extract the arguments */
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

  /* Verify that there is nothing left */
  if(argc > optind) {
    *err = tooMuchOption;
    return argc;
  }

  /* Do we have all the files ? */
  if ((!human_readable) && ((error_filename == NULL) || (free_filename == NULL) ||(markov_filename == NULL))) {
    *err = needfiles;
    return -1;
  }

  /* Create the submodules */
  markov = new ParamMarckovChain();
  onoff = new ParamBasicOnOff();

  /* And k ? */
  if (k == 0) {
    *err = markov->knotset;
    return -1;
  }

  /* Module initialization */
  current_state = false;
  length = 0;
  length_error = 0;
  second_round = false;
  C = 0;
  /* Sub-module initialization */
  markov->init(k, markov_filename);
  return onoff->init(error_filename, free_filename);
}

void
ParamBasicMTA::clean(void)
{
  /* Clean sub-modules */
  onoff->clean();
  markov->clean();
}

int
ParamBasicMTA::addChar(const bool input)
{
  if (second_round) {
    /* Second round */
    if (input == current_state) {
      /* Same input as before */
      if (!input) {
        /* If it's an error, we can directly add it to the markov chain */
        markov->addChar(input);
      }
      /* Increase the length of the current state */
      ++length;
    } else {
      /* We will change the 'current_state' at the end of the call, thus we need to clean the current one */
      if (current_state) {
        /* It's error-free but are we above or below the threshold ? */
        if (length > C) {
          /* Above the threshold: it's totally error-free */
          if (length_error != 0) {
            /* We probably had an error burst before that we didn't clean, thus push it now */
            onoff->addChars(false, length_error);
          }
          /* Clean the error length: we have no error left */
          length_error = 0;
          /* Add the error-free period */
          onoff->addChars(true, length);
        } else {
          /* Below the threshold: count as error */
          uint32_t temp;
          for (temp = 0; temp < length; ++temp) {
            /* Update the markov chain describing the error */
            markov->addChar(true);
          }
          /* Add it to the error buffer */
          length_error += length;
        }
        /* The new input is an error, we can directly add it to the markov chain */
        markov->addChar(input);
      } else {
        /* Last periode was an error period, add it to the error buffer */
        length_error += length;
      }
      /* Now the current state have only one element */
      length = 1;
    }
  } else {
    /* First round: use sub-module On-Off to evaluate the error-free bursts lengths (without any threshold) */
    onoff->addChar(input);
  }
  /* Remember the last input */
  current_state = input;
  return 0;
}

bool
ParamBasicMTA::nextRound()
{
  assert(!second_round);
  /* Consider the last bit */
  onoff->addChar(!current_state);
  /* Calculate the threshold C */
  double mean = 0, standard_deviation = 0, temp, total = (double)onoff->getRawErrorBurstNumber();
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
  C = (uint32_t)total;
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
  /* Need to clean the buffer, see addChar */
  assert(second_round);
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
  /* Finalize the submodules */
  onoff->finalize(max_rand); // This doesn't flush any internal state. As we only used addChars and not addChar, onoff->length = 0
  markov->finalize(max_rand);
}

void
ParamBasicMTA::printBinary(void)
{
  /* Use the sub-modules to print in binary format */
  onoff->printBinary();
  markov->printBinary();
}

void
ParamBasicMTA::printHuman(const uint32_t max_rand)
{
  /* Use the sub-modules to print in human-readable format */
  onoff->printHuman(max_rand);
  markov->printHuman(max_rand);
}
