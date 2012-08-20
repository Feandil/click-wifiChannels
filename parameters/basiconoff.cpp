/** @file basiconoff.cpp Implementation of the Basic On-Off parameter generation module */

#include "basiconoff.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

const struct option ParamBasicOnOff::long_options[] = {
  {"free",        required_argument, 0,  'f' },
  {"err",         required_argument, 0,  'r' },
  {NULL,                          0, 0,   0  }
};

const char * const ParamBasicOnOff::needfiles = "BasicOnOff needs 2 output files on non human-readable output";

int
ParamBasicOnOff::init(const char * const filename_error, const char * const filename_free)
{
  /* Change the filenames if needed */
  if (filename_error != NULL) {
    error_filename = filename_error;
    free_filename = filename_free;
  }
  /* Initialize various internal variables */
  success_total = 0;
  error_total = 0;
  current_state = false;
  length = 0;
  return 0;
}

int
ParamBasicOnOff::init(const int argc, char **argv, const bool human_readable, const char** err)
{
  int opt;
  optind = 1;
  error_filename = NULL;
  free_filename = NULL;
  /* Extract arguments */
  while((opt = getopt_long(argc, argv, "", long_options, NULL)) != -1) {
    switch(opt) {
      case 'f':
         free_filename = optarg;
        break;
      case 'r':
         error_filename = optarg;
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
  /* Do we have enough filenames ? */
  if ((!human_readable) && ((error_filename == NULL) || (free_filename == NULL))) {
    *err = needfiles;
    return -1;
  }
  /* Use the other init funtion to initialize internal variables */
  return init(NULL, NULL);
}

void
ParamBasicOnOff::clean(void)
{
  /* Clear all the maps */
  success_length.clear();
  error_length.clear();
  success_length_final.clear();
  error_length_final.clear();
}

int
ParamBasicOnOff::addChar(const bool input)
{
  if (input == current_state) {
    /* If we are still in the same state, increase the stored counter */
    ++length;
  } else {
    /* The state is changing, use the other function to flush the internal counter */
    /* Length can be null at the begining */
    if (length) {
      addChars(current_state, length);
    }
    /* Reset counter and state */
    current_state = input;
    length = 1;
  }
  return 0;
}

int
ParamBasicOnOff::addChars(const bool input, const uint32_t len)
{
  std::map<uint32_t, uint64_t>::iterator temp;
  if (input) {
    /* It's the end of an error-free burst, try to increase the corresponding mapped entry */
    temp = success_length.find(len);
    if (temp == success_length.end()) {
      /* The entry didn't exist, create it */
      success_length.insert(std::pair<uint32_t,uint64_t>(len, 1));
    } else {
      ++((*temp).second);
    }
    ++success_total;
  } else {
    /* It's the end of an error burst, try to increase the corresponding mapped entry */
    temp = error_length.find(len);
    if (temp == error_length.end()) {
      /* The entry didn't exist, create it */
      error_length.insert(std::pair<uint32_t,uint64_t>(len, 1));
    } else {
      ++((*temp).second);
    }
    ++error_total;
  }
  return 0;
}

bool
ParamBasicOnOff::nextRound()
{
  /* It's a one-pass algorithm */
  return false;
}


void
ParamBasicOnOff::calculate_values(const uint32_t manx_rand, const std::map<uint32_t, uint64_t> &map, const uint64_t total, std::map<uint32_t, uint32_t> &final)
{
  uint64_t temp_total = 0;
  std::map<uint32_t, uint64_t>::const_iterator it;

  for (it = map.begin(); it != map.end(); ++it) {
    temp_total += (*it).second;
    final.insert(std::pair<uint32_t,uint32_t>(it->first, (uint32_t)(((long double) temp_total) / ((long double) total) * ((long double) manx_rand))));
  }
}

void
ParamBasicOnOff::finalize(const uint32_t max_rand)
{
  /* Flush the last entry */
  addChar(!current_state);
  /* Create the final tables */
  calculate_values(max_rand, success_length, success_total, success_length_final);
  calculate_values(max_rand, error_length, error_total, error_length_final);
}

//! Try to write something to output and detect any error
#define WRITE(x)                                             \
  output << x << std::endl;                                  \
  if (output.bad()) {                                        \
    std::cerr << "error when writing to output" <<std::endl; \
    exit (-1);;                                              \
  }
//"

void
ParamBasicOnOff::printBinaryToFile(const std::map<uint32_t, uint32_t> &map, const char* dest)
{
  std::ofstream output;
  output.open(dest);
  uint32_t size = map.size();
  WRITE(size)
  std::map<uint32_t, uint32_t>::const_iterator it;
  for (it = map.begin(); it != map.end(); ++it) {
    WRITE(it->first);
    WRITE(it->second);
  }
  output.close();
}

void
ParamBasicOnOff::printBinary(void)
{
  printBinaryToFile(success_length_final, free_filename);
  printBinaryToFile(error_length_final, error_filename);
}

void
ParamBasicOnOff::printHumanToStream(const uint32_t max_rand, const std::map<uint32_t, uint32_t> &map, std::ostream &streamout)
{
  streamout << "(MaxRand: 0x" << std::hex << max_rand << ")" << std::endl;
  streamout << "CDF size: " << map.size() << std::endl;

  std::map<uint32_t, uint32_t>::const_iterator it;
  for (it = map.begin(); it != map.end(); ++it) {
    streamout << "- " << it->first << ": 0x" << std::hex << it->second << " (" << ((long double)it->second/((long double) max_rand))*100 << "%)" << std::endl;
  }
}

void
ParamBasicOnOff::printHuman(const uint32_t max_rand)
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
}
