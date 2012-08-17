#ifndef BASICMTA_H
#define BASICMTA_H

#include "module.h"
#include <getopt.h>
#include <map>
#include <iostream>
#include <fstream>

#include "markovchain.h"
#include "basiconoff.h"

/**
 * Extract a Basic MTA representation.
 * Basic MTA is a "simplified" version of the standard MTA model where instead of trying to fit a mathematical representation of the distribution of the
 * error/error-free lengths, it directly return the CDF of those.
 */
class ParamBasicMTA : public ParamModule {

  private:
    /**
     * Long options used by getopt_long.
     */
    static const struct option long_options[];
    /**
     * Error message: This module need more files
     */
    static const char * const needfiles;

    //! State of the last packet (success/error)
    bool current_state;
    //! Duration of the last state (number of consecutive packets in that state)
    uint32_t length;
    //! Duration of the last concatenated error period
    uint32_t length_error;

    //! Are-we in the second round or the first ?
    bool second_round;
    //! Markov chain used to caracterize the concatenated error bursts
    ParamMarckovChain *markov;
    //! Basic On-Off model used to caracterize the distributions of error/error-free bursts
    ParamBasicOnOff   *onoff;
    //! Threshold between error-free error-free bursts and error error-free bursts
    uint32_t C;

    /* Output */
    //! Ouput file for the error bursts distribution
    const char *error_filename;
    //! Ouput file for the error-free bursts distribution
    const char *free_filename;
    //! Ouput file for the Markov chain of the concatenated error bursts
    const char *markov_filename;


  public:
    /* Methodes of ParamModule */
    int init(const int, char **, const bool, const char**);
    void clean();
    int addChar(const bool);
    bool nextRound();
    void finalize(const uint32_t);
    void printBinary();
    void printHuman(const uint32_t);

    //! Name of this module
    static const char* name() { return "basicmta"; }
};

#endif
