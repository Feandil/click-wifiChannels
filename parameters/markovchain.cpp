#include "markovchain.h"

#include <inttypes.h>
#include <stdlib.h>
#include <getopt.h>
#include <iostream>
#include <fstream>

const char * const ParamMarckovChain::knotset = "K need to be set != (-k option)";

int
ParamMarckovChain::init(const int argc, char *argv[], const bool human_readable, const char ** err)
{
  int opt;
  optind = 1;
  k = 0;
  while((opt = getopt(argc, argv, "k:")) != -1) {
    switch(opt) {
      case 'k':
        k = atoi(optarg);
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
  state_mod = 1 << k;
  state = 0;
  states = new uint64_t[state_mod << 1];
  transitions = new uint32_t[state_mod];
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

  for (i = 0; i < state_mod; ++i) {
    tmp = (i << 1);
    if (states[tmp + 1] == 0) {
      transitions[i] = 0;
    } else {
      transitions[i] = (uint32_t) (((long double) states[tmp + 1]) / ((long double) states[tmp + 1] + states[tmp]) * ((long double) manx_rand));
    }
  }
}

void
ParamMarckovChain::printBinary()
{
  uint32_t temp;
#define WRITE4(x)  if (write(1,x,4) != 4) fprintf(stderr, "error when writing to output");
   WRITE4(&state_mod)
  for (temp = 0; temp < state_mod; ++temp) {
    WRITE4(transitions + temp)
  }
}

void
ParamMarckovChain::printHuman(const uint32_t max_rand)
{
  uint32_t temp;
  printf("(MaxRand: 0x%"PRIx32")\n", max_rand);
  printf("State Number : %"PRIu32"\n", state_mod);
  printf("Probability of success of transmission in state:\n");
  for (temp = 0; temp < state_mod; ++temp) {
    printf("- %"PRIx32": 0x%"PRIx32" (%Lg%%)\n", temp, transitions[temp], ((long double)transitions[temp]/((long double) max_rand))*100);
  }
}