/** @file main.cpp Entry point for the modules */

#include "module.h"

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <getopt.h>
#include <iostream>
#include <fstream>

//! Default value for CLICK_RAND_MAX used by click on linux plateforms
#define DEFAULT_MAX_RAND 0x7FFFFFFFU

/**
 * Print a short howto and exit.
 * @param err Execution code to return.
 */
static void usage(int err)
{
  std::ostream *output;
  if (err) {
    output = &std::cout;
  } else {
    output = &std::cerr;
  }
  *output << "parseInput: Parse an input onto a statistic representation" << std::endl;
  *output << "Usage: ./parseInput [OPTIONS] CLASS [CLASS_OPTIONS]" << std::endl;
  *output << "           Try to transform the input onto a static representation of class CLASS" << std::endl;
  *output << "Options:" << std::endl;
  *output << "     --help           Print this ..." << std::endl;
  *output << " -h, --human-readable Do not output Binary representation but human readable representation" << std::endl;
  *output << " -m, --max_rand <max> Specify the CLICK_RAND_MAX used by click (Default value 0x%" << DEFAULT_MAX_RAND << " )" << std::endl;
  *output << " -i, --input <file>   Specify the input file" << std::endl;
  *output << "Supported class with subotions:" << std::endl;
  *output << " * markovchain: k-order Marchov chain representation (2^k states)" << std::endl;
  *output << "   -k <k>             Order of the Markov chain" << std::endl;
  *output << "   -o <filename>      File used as the output (only if !-h)" << std::endl;
  *output << " * basiconoff: On-Off representation without cdf mathematic determination" << std::endl;
  *output << "       --free <file>  Filename used for error-free burst length cdf" << std::endl;
  *output << "       --err  <file>  Filename used for error burst length cdf" << std::endl;
  *output << " * basicmta: Markov-based Trace Analysis representation without cdf mathematic determination" << std::endl;
  *output << "   -k <k>             Order of the internal markov chain" << std::endl;
  *output << "       --free <file>  Filename used for error-free burst length cdf" << std::endl;
  *output << "       --err  <file>  Filename used for error burst length cdf" << std::endl;
  *output << "       --markov <f>   Filename used for the internal markovchain output" << std::endl;

  exit(err);
}

/**
 * Long options used by getopt_long; see 'usage' for more detail.
 */
static const struct option long_options[] = {
  {"human-readable",    no_argument, 0,  'h' },
  {"help",              no_argument, 0,  'e' },
  {"input",       required_argument, 0,  'i' },
  {"max_rand",    required_argument, 0,  'm' },
  {NULL,                          0, 0,   0  }
};

#include "markovchain.h"
#include "basiconoff.h"
#include "basicmta.h"

/**
 * Read all the istream and feed it to the ParamModule
 * @param in istream to read
 * @param mod Module to feed
 * @return : 0K : 0, error-code if != 0
 */
static int extract(std::istream *in, ParamModule *mod)
{
  char buf;
  int ret;
  in->get(buf);
  while(in->good()) {
    if (buf == '0') {
      ret = mod->addChar(false);
    } else if (buf == '1') {
      ret = mod->addChar(true);
    } else if (buf == '\n') {
      ret = 0;
    } else {
      std::cerr << "Parsing error : unauthorized char (" << buf << ")" << std::endl;
      return -6;
    }
    if (ret) {
      std::cerr << "Parsing error" << ret << std::endl;
      return ret;
    }
    in->get(buf);
  }
  return 0;
}

/**
 * Main system entry point
 * @param argc Argument Count
 * @param argv Argument Vector
 * @return Execution return code
 */
int main(int argc, char *argv[])
{
  bool human_readable;
  const char *err_message, *input_file;
  int opt, ret;
  uint32_t max_rand;
  ParamModule *mod;
  std::istream *in;
  std::ifstream *fin = NULL;

  /* Default values */
  human_readable = 0;
  max_rand = DEFAULT_MAX_RAND;
  input_file = NULL;

  while((opt = getopt_long(argc, argv, "+hm:i:", long_options, NULL)) != -1) {
    switch(opt) {
      case 'e':
        usage(0);
        break;
      case 'h':
        human_readable = true;
        break;
      case 'i':
        input_file = optarg;
        break;
      case 'm':
        if (max_rand == DEFAULT_MAX_RAND) {
          usage(1);
        }
        sscanf(optarg, "%"SCNu32, &max_rand);
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
    /* Try to find the module */
    if (strcmp(argv[optind], ParamMarckovChain::name()) == 0) {
      mod = new ParamMarckovChain();
    } else if (strcmp(argv[optind], ParamBasicOnOff::name()) == 0) {
      mod = new ParamBasicOnOff();
    } else if (strcmp(argv[optind], ParamBasicMTA::name()) == 0) {
      mod = new ParamBasicMTA();
    } else {
      std::cerr << "Unknown Module" << std::endl;
      return -1;
    }
    /* Initialize the module */
    ret = mod->init(argc - optind, argv + optind, human_readable, &err_message);
    if (ret) {
      fprintf(stderr, "%s (%i)\n", err_message, ret);
      return ret;
    }
  }

  /* Open the input file or use the standard input */
  if (input_file != NULL) {
    fin = new std::ifstream(input_file);
    in = fin;
  } else {
    in = &std::cin;
  }

  /* Verify input state */
  if (in->bad()) {
    std::cerr << "Unable to read input" << std::endl;
  }

  /* First run */
  extract(in, mod);

  /* Do a second run if needed */
  if (mod->nextRound()) {
    if (input_file == NULL) {
      std::cerr << "2nd round needed, input file needed" << std::endl;
      return -13;
    }
    fin->close();
    fin->open(input_file);
    extract(in, mod);
  }

  /* Close input */
  if (input_file != NULL) {
    fin->close();
  }

  /* Finalize module */
  mod->finalize(max_rand);

  /* Print */
  if (human_readable) {
    mod->printHuman(max_rand);
  } else {
    mod->printBinary();
  }

  /* Clean the module */
  mod->clean();
  return 0;
}
