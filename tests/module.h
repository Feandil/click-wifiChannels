#ifndef MODULE_H
#define MODULE_H

#define __STDC_FORMAT_MACROS
#include <stdint.h>
#include <iostream>
#include <fstream>

class TestRandom {
  private:
    uint32_t mod;
    std::ifstream *urandom;
  
  public:
    TestRandom();
    TestRandom(uint32_t);
    ~TestRandom();
    uint32_t random();
  
};

class TestModule {

  protected:

    /* common error messages */
    static const char * const unknownOption;
    static const char * const tooMuchOption;

  public:

    /* Configure the Element */
    virtual int configure(const int, char **, const char**) = 0;

    /* Initialize/cleanup the Element, called after the configure */
    virtual int initialize(TestRandom&) = 0;
    virtual void cleanup() = 0;

    /* generate packet */
    virtual int generate() = 0;
};

#endif
