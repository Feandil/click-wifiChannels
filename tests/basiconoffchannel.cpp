#define __STDC_LIMIT_MACROS
#include "basiconoffchannel.h"
#include <assert.h>
#include <iostream>
#include <fstream>
#include <limits.h>
#include <getopt.h>
#include <inttypes.h>

const struct option BasicOnOffChannel::long_options[] = {
  {"free",        required_argument, 0,  'f' },
  {"err",         required_argument, 0,  'r' },
  {NULL,                          0, 0,   0  }
};


const char * const BasicOnOffChannel::needfiles  = "BasicOnOff needs 2 intput files";

#if __WORDSIZE == 64
# define SIZE(in)              \
  ({                           \
    size_t temp = in.size();   \
    assert(temp < UINT32_MAX); \
    (uint32_t) temp;           \
  })
#else /* __WORDSIZE == 64 */
# define SIZE(in) \
    in.size()
#endif /* __WORDSIZE == 64 */


int
BasicOnOffChannel::thresholdrand (const std::vector<CDFPoint> &distribution)
{
  uint32_t rand, min, max, pos;
  
  rand = myRand.random();
  min = 0;
  max = SIZE(distribution) - 1;
  pos = SIZE(distribution) / 2;
  while (max - min != 1) {
    if (rand > distribution[pos].probability) {
      min = pos;
      pos = min + (max - min) / 2;
    } else {
      max = pos;
      pos = min + (max - min) / 2;
    }
  }

  if (rand > distribution[min].probability) {
    return distribution[max].point;
  } else {
    return distribution[min].point;
  }
}

int
BasicOnOffChannel::configure(const int argc, char **argv, const char** err)
{
  int opt;
  optind = 1;
  _error_free_cdf_filename = NULL;
  _error_cdf_filename = NULL;
  while((opt = getopt_long(argc, argv, "", long_options, NULL)) != -1) {
    switch(opt) {
      case 'f':
        _error_free_cdf_filename = optarg;
        break;
      case 'r':
        _error_cdf_filename = optarg;
        break;
      default:
        *err = unknownOption;
        return opt;
    }
  }
  
  if(argc > optind) {
    *err = tooMuchOption;
    return argc;
  }
  
  if ((_error_free_cdf_filename == NULL) || (_error_cdf_filename == NULL)) {
    *err = needfiles;
    return -1;
  }

  return 0;
}

void
BasicOnOffChannel::configure(const char * const free, const char * const err)
{
  _error_free_cdf_filename = free;
  _error_cdf_filename = err;
}

int
BasicOnOffChannel::load_cdf_from_file(const char *filename, std::vector<CDFPoint> &dist)
{
  #define UINT32_SIZE_IN_DEC 10
  char buf[UINT32_SIZE_IN_DEC + 1];
  uint32_t buffer;
  uint32_t len;
  CDFPoint point;
  
  std::ifstream ff;
  ff.open(filename);
  if (ff.fail()) {
    return -1;
  }
  ff.getline(buf, UINT32_SIZE_IN_DEC + 1);
  if (ff.fail() || (sscanf(buf, "%"SCNu32, &len) != 1)) {
    ff.close();
    return -2;
  }
  dist.reserve(len);
  
  while (len != 0) {
    --len;
    ff.getline(buf, UINT32_SIZE_IN_DEC + 1);
    if (ff.fail() || (sscanf(buf, "%"SCNu32, &buffer) != 1)) {
      ff.close();
      dist.clear();
      return -3;
    }
    if (buffer > INT_MAX) {
      ff.close();
      dist.clear();
      return -4;
    }
    point.point = (int) buffer;
    ff.getline(buf, UINT32_SIZE_IN_DEC + 1);
    if (ff.fail() || (sscanf(buf, "%"SCNu32, &buffer) != 1)) {
      ff.close();;
      dist.clear();
      return -5;
    }
    point.probability = buffer;
    dist.push_back(point);
  }
  ff.close();
  return 0;
}

int
BasicOnOffChannel::initialize(TestRandom& rand)
{
  myRand = rand;

  /* Initialize state */
  _remaining_length_in_state = 0;
  _current_state = myRand.random() < _initial_error_probability;

  return (load_cdf_from_file(_error_cdf_filename, _error_burst_length) || load_cdf_from_file(_error_free_cdf_filename, _error_free_burst_length));
}

void
BasicOnOffChannel::cleanup()
{
  _error_burst_length.clear();
  _error_free_burst_length.clear();
}

int
BasicOnOffChannel::generate ()
{
  /* Evaluate the remaining time if we need to */
  if (_remaining_length_in_state <= 0) {
    _current_state = !_current_state;
    if (_current_state) {
      _remaining_length_in_state = thresholdrand(_error_free_burst_length);
	} else {
      _remaining_length_in_state = thresholdrand(_error_burst_length);
    }
  }
  
  /* Decrease the remaining length in current state/sub-state */
  --_remaining_length_in_state;
  
  /* Drop or transmit depending on the sub-state */
  if (_current_state) {
    return 1;
  } else {
    return 0;
  }
}
