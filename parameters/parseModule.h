#ifndef PARSE_MODULE_H
#define PARSE_MODULE_H

#include <inttypes.h>

#ifdef __cplusplus
extern "C" {
#else
const char *unknownOption;
const char *tooMuchOption;
#endif

struct module {
  int (*init) (const int, char **, char, const char**);
  int (*addChar) (const int);
  void (*printBinary) (const uint32_t);
  void (*printHuman) (const uint32_t);
  void (*clean) (void);
};

struct module* init(char *name);

struct module* initMarkovChain();
struct module* initBasicOnOff();

#ifdef __cplusplus
}
#endif

#endif
