#ifndef DEBUG_H
#define DEBUG_H

#ifdef DEBUG
  #include <stdio.h>
  #define PERROR(x) perror(x);
  #define PRINTF(...) printf(__VA_ARGS__);
#else
  #define PERROR(x)
  #define PRINTF(...)
#endif

#endif /* DEBUG_H */
