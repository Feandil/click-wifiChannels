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
  fprintf(output, "Usage: ./parseInput [OPTIONS] CLASS\n");
  fprintf(output, "           Try to transform the input onto a static representation of class CLASS\n");
  fprintf(output, "Options:\n");
  fprintf(output, "     --help           Print this ...\n");
  fprintf(output, " -h, --human-readable Do not output Binary representation but human readable representation\n");
  fprintf(output, " -p <parameter>       Specify a parameter for the representation (default 0)\n");
  fprintf(output, " -m, --max_rand <max> Specify the CLICK_RAND_MAX used by click (Default value 0x%"PRIx32" )\n", DEFAULT_MAX_RAND);
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
  int opt,
      buf,
      ret,
    param;
  uint32_t max_rand;
  struct module* mod;
  
  /* Default values */
  human_readable = 0;
  param = 0;
  max_rand = DEFAULT_MAX_RAND;

  while((opt = getopt_long(argc, argv, "hp:m:", long_options, NULL)) != -1) {
    switch(opt) {
      case 'e':
        usage(0);
        break;
      case 'h':
        human_readable = 1;
        break;
      case 'p':
        param = atoi(optarg);
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

  if((argc <= optind) || (argc > optind + 1))
  {
    usage(1);
    return 1;
  } else {
    mod = init(argv[optind]);
    if (mod == NULL) {
      fprintf(stderr,"Unknown Module '%s'\n", argv[optind]);
      return 2;
    }
    ret = mod->init(param);
    if (ret) {
      fprintf(stderr, "Error durind module initialiezation : %i\n", ret);
      if (param == 0) {
        fprintf(stderr, "  (Could be missing parameter)\n");
      }
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
