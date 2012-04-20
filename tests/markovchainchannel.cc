#include "markovchainchannel.hh"
#include <iostream>
#include <fstream>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>

const char *markovunknownOption = "An unknown option was passed to the Module";
const char *markovtooMuchOption = "Too much option where passed to the module";
const char *markovneedfiles  = "BasicOnOff needs 2 intput files";

int
MarkovChainChannel::configure(const int argc, char **argv, const char** err)
{
  int opt;
  optind = 1;
  while((opt = getopt(argc, argv, "i:")) != -1) {
    switch(opt) {
      case 'i':
        _current_state = atoi(optarg);
        break;
      default:
        *err = markovunknownOption;
        return opt;
    }
  }
  
  if(argc <= optind) {
    *err = markovneedfiles;
    return -1;
  }
  
  filename = argv[optind];
  
  if (argc > optind + 1) {
    *err = markovtooMuchOption;
    return -2;
  }
  
  return 0;
}

int
MarkovChainChannel::initialize(TestRandom& rand)
{
  myRand = rand;

  #define UINT32_SIZE 4
  uint32_t buffer;
  uint32_t len;
  
  std::ifstream ff;
  ff.open(filename);
  if (ff.fail()) {
    return -1;
  }
  ff.read((char*)&len, UINT32_SIZE); 
  
  _success_probablilty.reserve(len);
  _state_modulo = len;
  
  while (len != 0) {
    --len;
    ff.read((char*)&buffer, UINT32_SIZE);  
    if (ff.fail()) {
      ff.close();
      _success_probablilty.clear();
      return -3;
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
