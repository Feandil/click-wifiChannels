#include "markovchainchannel.h"
#include <iostream>
#include <fstream>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

const char * const MarkovChainChannel::needfiles = "MarckChain needs 1 intput files";

int
MarkovChainChannel::configure(const int argc, char **argv, const char** err)
{
  if(argc <= 1) {
    *err = needfiles;
    return -1;
  }
  
  filename = argv[1];
  
  if (argc > 2) {
    *err = tooMuchOption;
    return -2;
  }
  
  return 0;
}

void
MarkovChainChannel::configure(const char * const file)
{
  filename = file;
}

int
MarkovChainChannel::initialize(TestRandom& rand)
{
  myRand = rand;

  #define UINT32_SIZE_IN_DEC 10
  char buf[UINT32_SIZE_IN_DEC + 1];
  uint32_t buffer;
  uint32_t len;
  
  std::ifstream ff;
  ff.open(filename);
  if (ff.fail()) {
    return -1;
  }
  ff.getline(buf, UINT32_SIZE_IN_DEC + 1);
  if (ff.fail() || (sscanf(buf, "%"SCNu32, &len) != 1)) {
    return -2;
  }
  
  _success_probablilty.reserve(len);
  _state_modulo = len;
  
  ff.getline(buf, UINT32_SIZE_IN_DEC + 1);
  if (ff.fail() || (sscanf(buf, "%"SCNu32, &_current_state) != 1)) {
    return -3;
  }
  
  while (len != 0) {
    --len;
    ff.getline(buf, UINT32_SIZE_IN_DEC + 1);
    if (ff.fail() || (sscanf(buf, "%"SCNu32, &buffer) != 1)) {
      ff.close();
      _success_probablilty.clear();
      return -4;
    }
    _success_probablilty.push_back(buffer);
  }
  ff.close();
  return 0;
}

void
MarkovChainChannel::cleanup()
{
  _success_probablilty.clear();
}

int
MarkovChainChannel::generate ()
{
  /* Evaluate the transmission */
  bool transmit = myRand.random() < _success_probablilty[_current_state];
  
  /* Update the state */
  _current_state = ((_current_state << 1) + transmit) % _state_modulo;
  
  /* Drop or transmit */
  if (transmit) {
    return 1;
  } else {
    return 0;
  }
}
