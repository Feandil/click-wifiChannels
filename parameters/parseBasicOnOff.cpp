#define __STDC_FORMAT_MACROS
#include "parseModule.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <getopt.h>
#include <iostream>
#include <fstream>
#include <map>

uint64_t success_total;
std::map<uint32_t, uint64_t> success_length;
uint64_t error_total;
std::map<uint32_t, uint64_t> error_length;

int last_int;
uint64_t length;

const char *error_filename, *free_filename;

const struct option basiconoff_long_options[] = {
  {"free",        required_argument, 0,  'f' },
  {"err",         required_argument, 0,  'r' },
  {NULL,                          0, 0,   0  }
};

const char *unknownOptionpp = "An unknown option was passed to the Module";
const char *tooMuchOptionpp = "Too much option where passed to the module";
const char *basicneedfiles  = "BasicOnOff needs 2 output files on non human-readable output";

static std::map<uint32_t, uint32_t>* calculate_values(const uint32_t manx_rand, const std::map<uint32_t, uint64_t> &map, const uint64_t total)
{
  uint64_t temp_total = 0;    
  std::map<uint32_t, uint32_t> *rez = new std::map<uint32_t, uint32_t>();
  std::map<uint32_t, uint64_t>::const_iterator it;

  for (it = map.begin(); it != map.end(); ++it) {
    temp_total += (*it).second;
    (*rez).insert(std::pair<uint32_t,uint32_t>((*it).first, (uint32_t)(((long double) temp_total) / ((long double) total) * ((long double) manx_rand))));
  }
  return rez;
}

static int basiconoff_init(const int argc, char **argv, char human_readable, const char** err)
{
  int opt;
  optind = 1;
  error_filename = NULL;
  free_filename = NULL;
  while((opt = getopt_long(argc, argv, "", basiconoff_long_options, NULL)) != -1) {
    switch(opt) {
      case 'f':
         free_filename = optarg;
        break;
      case 'r':
         error_filename = optarg;
        break;
      default:
        *err = unknownOptionpp;
        return opt;
    }
  }
  
  if(argc > optind) {
    *err = tooMuchOptionpp;
    return argc;
  }
  
  if ((!human_readable) && ((error_filename == NULL) || (free_filename == NULL))) {
    *err = basicneedfiles;
    return -1;
  }
  
  success_total = 0;
  error_total = 0;
  last_int = 0;
  length = 0;
  return 0;
}

static int basiconoff_addChar(const int input)
{
  if (input == last_int) {
    ++length;
  } else {
    if (length) {
      std::map<uint32_t, uint64_t>::iterator temp;
      if (last_int) {
        temp = success_length.find(length);
        if (temp == success_length.end()) {
          success_length.insert(std::pair<uint32_t,uint64_t>(length, 1));
        } else {
          ++((*temp).second);
        }
        ++success_total;
      } else {
        temp = error_length.find(length);
        if (temp == error_length.end()) {
          error_length.insert(std::pair<uint32_t,uint64_t>(length, 1));
        } else {
          ++((*temp).second);
        }
        ++error_total;
      }
    }
    last_int = input;
    length = 1;
  }
  return 0;
}

static void basiconoff_printBinaryToFile(const uint32_t max_rand, const std::map<uint32_t, uint64_t> &map, const uint64_t total, const char* dest)
{
  std::map<uint32_t, uint32_t>* ret = calculate_values(max_rand, map, total);
  std::ofstream output;
  output.open(dest);
#define WRITE4(x)  if (output.write((char*)x,4).bad())  std::cerr << "error when writing to output(" << dest << ")" <<std::endl;
  uint32_t size = map.size();
  WRITE4(&size)
   
  std::map<uint32_t, uint32_t>::const_iterator it;
  for (it = (*ret).begin(); it != (*ret).end(); ++it) {
    WRITE4(&(*it).first);
    WRITE4(&(*it).second);
  }
  output.close();
  delete(ret);
}

static void basiconoff_printBinary(const uint32_t max_rand)
{
  basiconoff_addChar(!last_int);
  basiconoff_printBinaryToFile(max_rand, success_length, success_total, free_filename);
  basiconoff_printBinaryToFile(max_rand, error_length, error_total, error_filename);
}

static void basiconoff_printHumanToStream(const uint32_t max_rand, const std::map<uint32_t, uint64_t> &map, const uint64_t total, std::ostream &streamout)
{
  std::map<uint32_t, uint32_t>* ret = calculate_values(max_rand, map, total);

  streamout << "(MaxRand: " << max_rand << ")" << std::endl;
  streamout << "CDF size: " << map.size() << std::endl;

  std::map<uint32_t, uint32_t>::const_iterator it;
  for (it = (*ret).begin(); it != (*ret).end(); ++it) {
    streamout << "- " << (*it).first << ": " << (*it).second << " (" << ((long double)(*it).second/((long double) max_rand))*100 << "%)" << std::endl;
  }
  delete(ret);
}

static void basiconoff_printHuman(const uint32_t max_rand)
{
  std::ofstream output;
  
  basiconoff_addChar(!last_int);
  
  if (free_filename == NULL ) {
    std::cout << "Error-Free-Burst length cdf" << std::endl;
    basiconoff_printHumanToStream(max_rand, success_length, success_total, std::cout);
    std::cout << std::endl;
  } else {
    output.open(free_filename);
    basiconoff_printHumanToStream(max_rand, success_length, success_total, output);
    output.close();
  }
  if (error_filename == NULL ) {
    std::cout << "Error-Burst length cdf" << std::endl;
    basiconoff_printHumanToStream(max_rand, error_length, error_total, std::cout);
    std::cout << std::endl;
  } else {
    output.open(error_filename);
    basiconoff_printHumanToStream(max_rand, error_length, error_total, output);
    output.close();
  }
}

static void basiconoff_clean(void)
{
  success_length.clear();
  error_length.clear();
}

extern "C" struct module* initBasicOnOff(void)
{
  struct module *ret = (struct module *) malloc(sizeof(struct module));
  ret->init = basiconoff_init;
  ret->addChar = basiconoff_addChar;
  ret->printBinary = basiconoff_printBinary;
  ret->printHuman = basiconoff_printHuman;
  ret->clean = basiconoff_clean;
  return ret;
}