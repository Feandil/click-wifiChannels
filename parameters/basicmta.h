#ifndef BASICMTA_H
#define BASICMTA_H

#include "module.h"
#include <getopt.h>
#include <map>
#include <iostream>
#include <fstream>

#include "markovchain.h"
#include "basiconoff.h"

class ParamBasicMTA : public ParamModule {

  private:

    /* static stuff */
    static const struct option long_options[];
    static const char * const needfiles;

    /* Variables */
    bool current_state;
    uint32_t length;
    uint32_t length_error;

    bool second_round;
    ParamMarckovChain *markov;
    ParamBasicOnOff   *onoff;
    uint32_t C;

    /* Output */
    const char *error_filename, *free_filename, *markov_filename;


  public:

    /* Initialize the module */
    int init(const int, char **, const bool, const char**);

    /* Clean the module */
    void clean();

    /* Add input char */
    int addChar(const bool);

    /* Is-there a 2nd round ? (prepare the module to the potential 2nd round */
    bool nextRound();

    /* Finalise the data */
    void finalize(const uint32_t);

    /* Output */
    void printBinary();
    void printHuman(const uint32_t);

    /* name */
    static const char* name() { return "basicmta"; }
};

#endif
