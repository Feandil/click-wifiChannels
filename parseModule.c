#include <stdlib.h>
#include <string.h>
#include "parseModule.h"

static char markovChain[] = "markovchain";

struct module* init(char *name)
{
  struct module* ret = NULL;
  if (strcmp(name, markovChain) == 0) {
    ret = initMarkovChain();
  }
  return ret;
}
