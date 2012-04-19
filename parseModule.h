#ifndef PARSE_MODULE_H
#define PARSE_MODULE_H

#include <inttypes.h>

const char *unknownOption;
const char *tooMuchOption;

struct module {
  int (*init) (const int, char **, char, const char**);
  int (*addChar) (const int);
  void (*printBinary) (const uint32_t);
  void (*printHuman) (const uint32_t);
  void (*clean) (void);
};

struct module* init(char *name);

struct module* initMarkovChain();

#endif
