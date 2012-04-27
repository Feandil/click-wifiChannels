#include "basicmtachannel.h"
#include <iostream>
#include <fstream>
#include <limits.h>
#include <getopt.h>

const struct option BasicMTAChannel::long_options[] = {
  {"free",        required_argument, 0,  'f' },
  {"err",         required_argument, 0,  'r' },
  {"markov",      required_argument, 0,  'm' },
  {NULL,                          0, 0,   0  }
};

const char * const BasicMTAChannel::needfiles  = "BasicMTA needs 3 intput files";

int
BasicMTAChannel::configure(const int argc, char **argv, const char** err)
{
  int opt;
  optind = 1;
  char *onoff_free = NULL;
  char *onoff_err = NULL;
  char *markov_file = NULL;
  while((opt = getopt_long(argc, argv, "", long_options, NULL)) != -1) {
    switch(opt) {
      case 'f':
        onoff_free = optarg;
        break;
      case 'r':
        onoff_err = optarg;
        break;
      case 'm':
        markov_file = optarg;
        break;
      default:
        *err = unknownOption;
        return opt;
    }
  }
  
  if(argc > optind) {
    *err = tooMuchOption;
    return argc;
  }
  
  if ((onoff_free == NULL) || (onoff_err == NULL) || (markov_file == NULL)) {
    *err = needfiles;
    return -1;
  }

  markov.configure(markov_file);
  onoff.configure(onoff_free, onoff_err);
  return 0;
}

int
BasicMTAChannel::initialize(TestRandom& rand)
{
  return (onoff.initialize(rand) || markov.initialize(rand));
}

void
BasicMTAChannel::cleanup()
{
  onoff.cleanup();
  markov.cleanup();
}

int
BasicMTAChannel::generate ()
{
  if (onoff.generate()) {
    return 1;
  } else {
    if (markov.generate()) {
      return 1;
    } else {
      return 0;
    }
  }
}
