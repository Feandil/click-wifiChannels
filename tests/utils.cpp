#include "module.h"
#include <ios>

#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include <string.h>
#include <stdio.h>
#include <iostream>
#include <fstream>

#include "markovchainchannel.h"
#include "basiconoffchannel.h"
#include "basicmtachannel.h"

const char * const TestModule::unknownOption = "An unknown option was passed to the Module";
const char * const TestModule::tooMuchOption = "Too much option where passed to the module";

TestRandom::TestRandom(uint32_t max)
{
  mod = (max + 1);
  urandom = new std::ifstream("/dev/urandom", std::ios::in|std::ios::binary);
}

TestRandom::TestRandom()
{
  mod = 1 << 31;
  urandom = new std::ifstream("/dev/urandom", std::ios::in|std::ios::binary);
}

TestRandom::~TestRandom()
{
  delete(urandom);
}

uint32_t
TestRandom::random()
{
  uint32_t rez;
  (*urandom).read((char*)&rez, 4);
  if (mod) {
    rez %= mod;
  }
  return rez;
}

int main(int argc, char *argv[])
{
  const char* output = NULL;
  int opt, ret;
  
  uint64_t generated_length = 0;

  while((opt = getopt(argc, argv, "+o:s:")) != -1) {
    switch(opt) {
      case 'o':
        output = optarg;
        break;
      case 's':
        sscanf(optarg, "%"PRIu64, &generated_length);
        break;
      default:
        std::cerr << "Unkown parameter" << std::endl;
        return opt;
    }
  }
  
  if(argc <= optind) {
    std::cerr << "Missing module name" << std::endl;
    return argc;
  }

  TestModule *m;
  if (strcmp(argv[optind], MarkovChainChannel::name()) == 0) {
    m = new MarkovChainChannel();
  } else if (strcmp(argv[optind], BasicOnOffChannel::name()) == 0) {
    m = new BasicOnOffChannel();
  } else if (strcmp(argv[optind], BasicMTAChannel::name()) == 0) {
    m = new BasicMTAChannel();
  } else {
    std::cerr << "Unknown Module" << std::endl;
    return -1;
  }
  
  const char* err_message;
  ret = m->configure(argc - optind, argv + optind, &err_message);
  if (ret) {
    std::cerr << err_message << " (" << err_message << ")" << std::endl;
    return ret;
  }
  
  TestRandom rand;
  ret = m->initialize(rand);
  if (ret) {
    std::cerr << "Module initialization error (" << ret  << ")" << std::endl;
    return ret;
  }
  
  std::ostream *out;
  if (output != NULL) {
    
    out = new std::ofstream(output);
    if (out->fail()) {
      std::cerr << "Error opening output file" << std::endl;
    }
  } else {
    out = &std::cout;
  }
  
  for (; generated_length != 0; --generated_length) {
    (*out) << m->generate();
  }
  
  m->cleanup();  
  return 0;
}
