#include "markovchain.h"

#include <inttypes.h>
#include <stdlib.h>
#include <getopt.h>
#include <iostream>
#include <fstream>

const char * const ParamMarckovChain::knotset = "K need to be set != (-k option)";

void
ParamMarckovChain::init(const int kb, const char* const filename)
{
  k = kb;
  state_mod = 1 << k;
  state = 0;
  states = new uint64_t[state_mod << 1];
  transitions = new uint32_t[state_mod];
  if (filename != NULL) {
    output_filename = filename;
  }
}

int
ParamMarckovChain::init(const int argc, char *argv[], const bool human_readable, const char ** err)
{
  int opt;
  optind = 1;
  k = 0;
  output_filename = NULL;
  while((opt = getopt(argc, argv, "k:o:")) != -1) {
    switch(opt) {
      case 'k':
        k = atoi(optarg);
        break;
      case 'o':
        output_filename = optarg;
        break;
      default:
        *err = unknownOption;
        return opt;
    }
  }
  if (k == 0) {
    *err = knotset;
    return -1;
  }
  if(argc > optind) {
    *err = tooMuchOption;
    return argc;
  }
  init(k, NULL);
  return 0;
}

void
ParamMarckovChain::clean()
{
  delete[] (states);
  delete[] (transitions);
}

int
ParamMarckovChain::addChar(const bool input)
{
  if (k) {
    --k;
  } else {
    ++(states[(state << 1) + input]);
  }
  state <<= 1;
  state %= state_mod;
  state += input;
  return 0;
}

bool
ParamMarckovChain::nextRound()
{
  return false;
}

void
ParamMarckovChain::finalize(const uint32_t manx_rand)
{
  uint32_t   i, tmp;
  long double sum, max = 0;

  for (i = 0; i < state_mod; ++i) {
    tmp = (i << 1);
    sum = ((long double)states[tmp + 1]) + states[tmp];
    if (sum > max) {
      max = sum;
      state = i;
    }
    if (states[tmp + 1] == 0) {
      transitions[i] = 0;
    } else {
      transitions[i] = (uint32_t) (((long double) states[tmp + 1]) / sum * manx_rand);
    }
  }
}

void
ParamMarckovChain::printBinary()
{
  std::ostream *output;
  std::ofstream *output_f;
  if (output_filename == NULL ) {
    output_f = NULL;
    output = &std::cout;
  } else {
    output_f = new std::ofstream(output_filename);
    output = output_f;
  }
  uint32_t temp;
#define WRITE4(x)  if (output->write((char*)(x),4).bad()) { std::cerr << "error when writing to output" <<std::endl; exit (-1); }
   WRITE4(&state_mod)
  for (temp = 0; temp < state_mod; ++temp) {
    WRITE4(transitions + temp)
  }
  if (output_f != NULL ) {
    output_f->close();
  }
  std::cout << "Most probable state : 0x" << std::hex << state << std::endl;
}

void
ParamMarckovChain::printHuman(const uint32_t max_rand)
{
  uint32_t temp;
  std::cout << "(MaxRand: 0x" << std::hex << max_rand << ")" << std::endl;
  std::cout << "State Number : 0x" << std::hex << max_rand << std::endl;
  std::cout << "Most probable state : 0x" << std::hex << state << std::endl;
  std::cout << "Probability of success of transmission in state:" << std::endl;
  for (temp = 0; temp < state_mod; ++temp) {
    std::cout << "- 0x" << std::hex << temp << ": 0x%" << std::hex << transitions[temp] << " (" << ((long double)transitions[temp]/((long double) max_rand))*100 << "%)" << std::endl;
  }
}