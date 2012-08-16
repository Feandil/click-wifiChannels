#ifndef DEBUG_H
#define DEBUG_H

/** @file debug.h Debug macros */

#ifdef DEBUG
  #include <stdio.h>
  //! perror debuging macro, only print if DEBUG is defined
  #define PERROR(x) perror(x);
  //! printf debuging macro, only print if DEBUG is defined
  #define PRINTF(...) printf(__VA_ARGS__);
#else
  //! perror debuging macro, only print if DEBUG is defined
  #define PERROR(x)
  //! printf debuging macro, only print if DEBUG is defined
  #define PRINTF(...)
#endif

#endif /* DEBUG_H */
