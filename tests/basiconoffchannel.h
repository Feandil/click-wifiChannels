#ifndef CLICK_BASICONOFFCHANNEL_HH
#define CLICK_BASICONOFFCHANNEL_HH

#define __STDC_FORMAT_MACROS
#include <stdint.h>
#include <vector>
#include <string>
#include "module.h"

class BasicOnOffChannel : public TestModule {

  private:

    /* Classe used to hold the Cumulative distribution functions */
    class CDFPoint {
      public:
        uint32_t probability;
        int point;
    };
  
    /* Variables used to store the statistic representation from the configuration files */
    uint32_t _initial_error_probability;
    std::vector<CDFPoint> _error_burst_length;
    std::vector<CDFPoint> _error_free_burst_length;
  
    const char *_error_cdf_filename;
    const char *_error_free_cdf_filename;
	
	  /* Load a CDF for a file */
    int load_cdf_from_file(const char *, std::vector<CDFPoint>&);

    /* Generate a random number from a Cumulative distribution functions */
    int thresholdrand(const std::vector<CDFPoint>&);
   
    /* Current state description */

    bool _current_state;  // True if error-free, false if error
    int _remaining_length_in_state;
  
    TestRandom myRand;
    
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
