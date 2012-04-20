#ifndef CLICK_MARKOVCHAINCHANNEL_HH
#define CLICK_MARKOVCHAINCHANNEL_HH

#define __STDC_FORMAT_MACROS
#include <stdint.h>
#include <vector>
#include "module.hh"

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
 
  public:

    /* Configure the Element */
    int configure(const int, char **, const char**);

    /* Initialize/cleanup the Element, called after the configure */
    int initialize(TestRandom&);
    void cleanup();

    /* generate packet */
    int generate();
};

#endif
