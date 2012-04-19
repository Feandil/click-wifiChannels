#include <stdlib.h>
#include <string.h>
#include "parseModule.h"

const char *unknownOption = "An unknown option was passed to the Module";
const char *tooMuchOption = "Too much option where passed to the module";

static char markovChain[] = "markovchain";
static char basicOnOff[]  = "basiconoff";

struct module* init(char *name)
{
  struct module* ret = NULL;
  if (strcmp(name, markovChain) == 0) {
    ret = initMarkovChain();
  } else if (strcmp(name, basicOnOff) == 0) {
    ret = initBasicOnOff();
  }
  return ret;
}
