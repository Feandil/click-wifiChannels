#include <click/config.h>
#include <click/args.hh>
#include <click/error.hh>
#include <click/glue.hh>

#include "basiconoffchannel.hh"

CLICK_DECLS

int
BasicOnOffChannel::thresholdrand (const Vector<CDFPoint> &distribution)
{
  uint32_t rand;
  int min,
      max,
      pos;
  
  rand = click_random();
  min = 0;
  max = distribution.size() - 1;
  pos = distribution.size() / 2;
  while ((pos != 0) && (pos != max)) {
    if (rand < distribution[pos].probability) {
      max = pos - 1;
      pos = min + (pos - min) / 2;
    } else if (rand > distribution[pos + 1].probability) {
      min = pos;
      pos = pos + (max - pos) / 2;
    } else {
      break;
    }
  }
  return distribution[pos].point;
}

void
BasicOnOffChannel::static_initialize()
{
  //Probably not needed:
  click_random_srandom();
}

int
BasicOnOffChannel::configure(Vector<String> &conf, ErrorHandler *errh)
{
#if CLICK_USERLEVEL || CLICK_TOOL
  if (Args(conf, this, errh)
      .read_m("ERROR_CDF_FILENAME", FilenameArg(), _error_cdf_filename)
      .read_m("ERROR_FREE_CDF_FILENAME", FilenameArg(), _error_free_cdf_filename)
      .read_m("INITIAL_ERROR_PROB", _initial_error_probability)
      .complete() < 0) {
    return -1;
  }
#else
  if (Args(conf, this, errh)
      .read_m("ERROR_CDF_FILENAME", _error_cdf_filename)
      .read_m("ERROR_FREE_CDF_FILENAME", _error_free_cdf_filename)
      .read_m("INITIAL_ERROR_PROB", _initial_error_probability)
      .complete() < 0) {
    return -1;
  }
#endif
  return 0;
}

int
BasicOnOffChannel::load_cdf_from_file(const String filename, ErrorHandler *errh, Vector<CDFPoint> &dist)
{
#define UINT32_SIZE 4
  uint32_t buffer;
  uint32_t len;
  CDFPoint point;
  
  _ff.filename() = filename;
  if (_ff.initialize(errh) < 0) {
    return -1;
  }
  
  if (_ff.read(&len, UINT32_SIZE, errh) != UINT32_SIZE) {
    _ff.cleanup();
    return -2;
  }
  dist.reserve(len);
  
  while (len != 0) {
    --len;
    if (_ff.read(&buffer, UINT32_SIZE, errh) != UINT32_SIZE) {
      _ff.cleanup();
      dist.clear();
      return -3;
    }
    if (buffer > INT_MAX) {
      _ff.cleanup();
      dist.clear();
      return -4;
    }
    point.point = (int) buffer;
    if (_ff.read(&buffer, UINT32_SIZE, errh) != UINT32_SIZE) {
      _ff.cleanup();
      dist.clear();
      return -5;
    }
    point.probability = buffer;
    dist.push_back(point);
  }
  _ff.cleanup();
  return 0;
}

int
BasicOnOffChannel::initialize(ErrorHandler *errh)
{
  load_cdf_from_file(_error_cdf_filename, errh, _error_burst_length);
  load_cdf_from_file(_error_free_cdf_filename, errh, _error_free_burst_length);
  
  /* Initialize state */
  _remaining_length_in_state = 0;
  _current_state = (double) click_random() < _initial_error_probability;
  return 0;
}

void
BasicOnOffChannel::cleanup(CleanupStage)
{
  _error_burst_length.clear();
  _error_free_burst_length.clear();
}

void
BasicOnOffChannel::push (int, Packet *p)
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
    output(0).push(p);
  } else {
    if (noutputs() == 2) {
      output(1).push(p);
    } else {
      p->kill();
    }
  }
}

CLICK_ENDDECLS
EXPORT_ELEMENT(BasicOnOffChannel)
