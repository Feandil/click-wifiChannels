#ifndef CLICK_MARKOVCHAINCHANNEL_HH
#define CLICK_MARKOVCHAINCHANNEL_HH

#define __STDC_FORMAT_MACROS
#include <stdint.h>
#include <vector>
#include "module.h"

class MarkovChainChannel : public TestModule {

  private:
    /* Variables used to store the statistic representation from the configuration files */
    std::vector<uint32_t> _success_probablilty;

    /* FileDescriptor */
    const char* filename;

    /* Current state description */
    uint32_t _current_state;
    uint32_t _state_modulo;

    TestRandom myRand;

    /* Configuration */
    static const char * const needfiles;

  public:

    /* Configure the Element */
    int configure(const int, char **, const char**);
    void configure(const char * const);

    /* Initialize/cleanup the Element, called after the configure */
    int initialize(TestRandom&);
    void cleanup();

    /* generate packet */
    int generate();

    /* name */
    static const char* name() { return "markovchain"; }
};

#endif
