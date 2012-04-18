#ifndef PARSE_MODULE_H
#define PARSE_MODULE_H

#include <inttypes.h>

struct module {
  int (*init) (const int);
  int (*addChar) (const int);
  void (*printBinary) (const uint32_t);
  void (*printHuman) (const uint32_t);
  void (*clean) (void);
};

struct module* init(char *name);

struct module* initMarkovChain();

#endif
