#ifndef MARKOVCHAIN_H
#define MARKOVCHAIN_H

#include "module.h"

class ParamMarckovChain : public ParamModule {

  private:

    /* Variables */
    int k;
    uint32_t state, state_mod, *transitions;
    uint64_t *states;

    /* Error String */
    static const char * const knotset;

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
};

#endif
