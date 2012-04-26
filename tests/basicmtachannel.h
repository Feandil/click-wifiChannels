#ifndef CLICK_BASICMTACHANNEL_HH
#define CLICK_BASICMTACHANNEL_HH

#include "module.h"
#include <stdlib.h>
#include "basiconoffchannel.h"
#include "markovchainchannel.h"

class BasicMTAChannel : public TestModule {

  private:

    BasicOnOffChannel onoff;
    MarkovChainChannel markov;

    static const char * const needfiles;
    static const struct option long_options[];

  public:

    /* Configure the Element */
    int configure(const int, char **, const char**);

    /* Initialize/cleanup the Element, called after the configure */
    int initialize(TestRandom&);
    void cleanup();

    /* receive packet from above */
    int generate();
};

#endif
