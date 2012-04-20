#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <getopt.h>

#include "parseModule.h"

static const char knotset[] = "K need to be set != (-k option)";

int k;
uint32_t    state,
       state_mod;
uint64_t *states;

static uint32_t* calculate_values(const uint32_t manx_rand)
{
  uint32_t   i,
           tmp;
  
  uint32_t* ret = malloc(state_mod * sizeof(uint32_t));
  if (ret == NULL) {
    return NULL;
  }
  for (i = 0; i < state_mod; ++i) {
    tmp = (i << 1);
    if (states[tmp + 1] == 0) {
      ret[i] = 0;
    } else {
      ret[i] = (uint32_t) (((long double) states[tmp + 1]) / ((long double) states[tmp + 1] + states[tmp]) * ((long double) manx_rand));
    }
  }
  return ret;
}

static int markov_init(const int argc, char *argv[], char human_readable, const char ** err)
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
  states = malloc ((state_mod << 1) * sizeof(uint64_t));
  if (states == NULL) {
    return -2;
  }
  return 0;
}

static int markov_addChar(const int input)
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

static void markov_printBinary(const uint32_t max_rand)
{
  uint32_t temp;
  uint32_t *ret = calculate_values(max_rand);
  if (ret == NULL) {
    fprintf(stderr, "malloc error\n");
    return;
  }
#define WRITE4(x)  if (write(1,x,4) != 4) fprintf(stderr, "error when writing to output");
   WRITE4(&state_mod)
  for (temp = 0; temp < state_mod; ++temp) {
    WRITE4(ret + temp)
  }
  free(ret);
}

static void markov_printHuman(const uint32_t max_rand)
{
  uint32_t temp;
  uint32_t *ret = calculate_values(max_rand);
  if (ret == NULL) {
    fprintf(stderr, "malloc error\n");
    return;
  }  
  printf("(MaxRand: 0x%"PRIx32")\n", max_rand);
  printf("State Number : %"PRIu32"\n", state_mod);
  printf("Probability of success of transmission in state:\n");
  for (temp = 0; temp < state_mod; ++temp) {
    printf("- %"PRIx32": 0x%"PRIx32" (%Lg%%)\n", temp, ret[temp], ((long double)ret[temp]/((long double) max_rand))*100);
  }
  free(ret);
}

static void markov_clean(void)
{
  free(states);
}

struct module* initMarkovChain(void)
{
  struct module *ret = malloc(sizeof(struct module));
  ret->init = markov_init;
  ret->addChar = markov_addChar;
  ret->printBinary = markov_printBinary;
  ret->printHuman = markov_printHuman;
  ret->clean = markov_clean;
  return ret;
}