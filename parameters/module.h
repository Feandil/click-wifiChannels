#ifndef MODULE_H
#define MODULE_H

#define __STDC_FORMAT_MACROS
#include <stdint.h>

class ParamModule {

  protected:
  
    /* common error messages */
    static const char * const unknownOption;
    static const char * const tooMuchOption;

  public:

    /* Initialize the module */
    virtual int init(const int, char **, const bool, const char**) = 0;
    
    /* Clean the module */
    virtual void clean() = 0;
    
    /* Add input char */
    virtual int addChar(const bool) = 0;
    
    /* Is-there a 2nd round ? (prepare the module to the potential 2nd round */
    virtual bool nextRound() = 0;
    
    /* Finalise the data */
    virtual void finalize(const uint32_t) = 0;

    /* Output */
    virtual void printBinary() = 0;
    virtual void printHuman(const uint32_t) = 0;
};

#endif
