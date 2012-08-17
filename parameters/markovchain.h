#ifndef MARKOVCHAIN_H
#define MARKOVCHAIN_H

#include "module.h"

/**
 * Extract a Markov-chain representation.
 * Produce the probability of success of the different states of a k-th order Markov Chain
 */
class ParamMarckovChain : public ParamModule {

  private:
    //! Order of the Markov state
    int k;
    /**
     * Current history in binary.
     * state & (1 << k) means that k + 1 step ago it was a success
     */
    uint32_t state;
    //! first state to forget, that is (1 << k)
    uint32_t state_mod;
    //! Number of occurences of the indexed state
    uint64_t *states;
    //! Probability, relatively to rand_max, to have a success in the indexed state
    uint32_t *transitions;
    //! File which will contain the generated parameters
    const char *output_filename;

  public:
    /* Methodes of ParamModule */
    int init(const int, char **, const bool, const char**);
    void clean();
    int addChar(const bool);
    bool nextRound();
    void finalize(const uint32_t);
    void printBinary();
    void printHuman(const uint32_t);

    /**
     * Special module-dependant initialization
     * @param k Order of the Markov chain
     * @param filename Name of the file used for printing the Markov chain representation
     */
    void init(const int k, const char* const filename);

    //! Error message: A k-th order Markov-chain need an order k
    static const char * const knotset;

    //! Name of this module
    static const char* name() { return "markovchain"; }
};

#endif
