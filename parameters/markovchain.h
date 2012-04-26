#ifndef MARKOVCHAIN_H
#define MARKOVCHAIN_H

#include "module.h"

class ParamMarckovChain : public ParamModule {

  private:

    /* Variables */
    int k;
    uint32_t state, state_mod, *transitions;
    uint64_t *states;
    const char * output_filename;

  public:

    /* Initialize the module */
    int init(const int, char **, const bool, const char**);
    void init(const int, const char* const);

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
    
    /* Error String */
    static const char * const knotset;

    /* name */
    static const char* name() { return "markovchain"; }
};

#endif
