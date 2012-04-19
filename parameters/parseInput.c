#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

#include "parseModule.h"

#define DEFAULT_MAX_RAND 0x7FFFFFFFU

static void usage(int err)
{
  FILE *output;
  if (err) {
    output = stderr;
  } else {
    output = stdout;
  }
  fprintf(output, "parseInput: Parse an input onto a statistic representation\n\n");
  fprintf(output, "Usage: ./parseInput [OPTIONS] CLASS [CLASS_OPTIONS]\n");
  fprintf(output, "           Try to transform the input onto a static representation of class CLASS\n");
  fprintf(output, "Options:\n");
  fprintf(output, "     --help           Print this ...\n");
  fprintf(output, " -h, --human-readable Do not output Binary representation but human readable representation\n");
  fprintf(output, " -m, --max_rand <max> Specify the CLICK_RAND_MAX used by click (Default value 0x%"PRIx32" )\n", DEFAULT_MAX_RAND);
  fprintf(output, "Supported class with subotions:\n");
  fprintf(output, " * markovchain: k-order Marchov chain representation (2^k states)\n");
  fprintf(output, "   -k <k>             Order of the Markov chain\n");
  fprintf(output, " * basiconoff: On-Off representation without cdf mathematic determination\n");
  fprintf(output, "       --free <file>  Filename used for error-free burst length cdf\n");
  fprintf(output, "       --err  <file>  Filename used for error burst length cdf\n");
  
  exit(err);
}

const struct option long_options[] = {
  {"human-readable",    no_argument, 0,  'h' },
  {"help",              no_argument, 0,  'e' },
  {"max_rand",    required_argument, 0,  'm' },
  {NULL,                          0, 0,   0  }
};

int main(int argc, char *argv[])
{
  char human_readable;
  const char *err_message;
  int opt,
      buf,
      ret;
  uint32_t max_rand;
  struct module* mod;
  
  /* Default values */
  human_readable = 0;
  max_rand = DEFAULT_MAX_RAND;

  while((opt = getopt_long(argc, argv, "+hm:", long_options, NULL)) != -1) {
    switch(opt) {
      case 'e':
        usage(0);
        break;
      case 'h':
        human_readable = 1;
        break;
      case 'm':
        if (max_rand == DEFAULT_MAX_RAND) {
          usage(1);
        }
        max_rand = atoi(optarg);
        break;
      default:
        usage(1);
        break;
    }
  }

  if(argc <= optind)
  {
    usage(1);
    return 1;
  } else {
    mod = init(argv[optind]);
    if (mod == NULL) {
      fprintf(stderr,"Unknown Module '%s'\n", argv[optind]);
      return 2;
    }
    ret = mod->init(argc - optind, argv + optind, human_readable, &err_message);
    if (ret) {
      fprintf(stderr, "%s (%i)\n", err_message, ret);
      return ret;
    }
  }
  
  buf = fgetc(stdin);
  while(buf != EOF) {
    if (buf == '0') {
      ret = mod->addChar(0);
    } else if (buf == '1') {
      ret = mod->addChar(1);
    } else if (buf == '\n') {
      ret = 0;
    } else {
      fprintf(stderr, "Parsing error : unauthorized char (%c)\n", buf);
      return -6;
    }
    if (ret) {
      fprintf(stderr, "Parsing error %i\n", ret);
      return ret;
    }
    buf = fgetc(stdin);
  }
  if (human_readable) {
    mod->printHuman(max_rand);
  } else {
    mod->printBinary(max_rand);
  }
  mod->clean();
  return 0;
}
