#include <click/config.h>
#include <click/confparse.hh>
#include <click/error.hh>
#include <click/glue.hh>
#include "kstatethresholdchannel.hh"
CLICK_DECLS

int
KStateThresholdChannel::thresholdrand (const cdf_t* cdf)
{
  double rand;
  int min,
      max,
	  pos;
  
  rand = (double) click_random() / (double) CLICK_RAND_MAX;
  min = 0;
  max = cdf->len - 1;
  pos = cdf->len / 2;
  while ((pos != 0) && (pos != max)) {
    if (rand < (cdf->points + pos)->probability) {
	  max = pos - 1;
	  pos = min + (pos - min) / 2;
	} else if (rand > (cdf->points + pos + 1)->probability) {
	  min = pos;
	  pos = pos + (max - pos) / 2;
    } else {
	  break;
	}
  }
  return (cdf->points + pos)->point;
}

/* Void (con|de)structor */
KStateThresholdChannel::KStateThresholdChannel()
{
  _error_free_burst_length = NULL;
  _error_burst_length = NULL;
}

KStateThresholdChannel::~KStateThresholdChannel() {}

void
KStateThresholdChannel::static_initialize()
{
  //Probably not needed:
  click_random_srandom();
}

void
KStateThresholdChannel::static_cleanup()
{
}

int
KStateThresholdChannel::configure(Vector<String> &conf, ErrorHandler *errh)
{
  //TODO: parse the conf
/*
  if (cp_va_parse(conf, this, errh,
		  cpKeywords,
		  "DEBUG", cpBool, "Debug", &_debug,
		  cpEnd) < 0)
    return -1;
*/
  return 0;
}

KStateThresholdChannel::cdf_t*
KStateThresholdChannel::load_cdf_from_file(String &filename, ErrorHandler *errh)
{
  _ff.filename() = filename;
    if (_ff.initialize(errh) < 0) {
	return NULL;
  }

  
  return NULL;
}



int
KStateThresholdChannel::initialize(ErrorHandler *errh)
{
  //TODO: load the probs
  /*_ff.filename = "zut";
  if (_ff.initialize(errh) < 0) {
	return -1;
  }
  
  _ff.cleanup();
  */
  
  /* Initialize state */
  _current_state = click_random(0, _nbstates - 1);
  _remaining_length_in_sub_state = 0;
  _remaining_lentgh_in_state = 0;
  return 0;
}

void
KStateThresholdChannel::cleanup(CleanupStage)
{
  cdf_t *temp;
  int len;
  
#define FREE_CDF(cdf)       \
  if (cdf != NULL) {        \
    len = _nbstates;        \
	temp = cdf;             \
	while (len > 0) {       \
	  free(temp->points);   \
	  ++cdf;                \
	  --len;                \
	}                       \
	cdf = NULL;             \
  }
	
  FREE_CDF(_error_free_burst_length)
  FREE_CDF(_error_burst_length)
  
  //TODO: unload the state change probs
}

void
KStateThresholdChannel::push (int, Packet *p)
{
  if (_remaining_length_in_sub_state == 0) {
    if (_remaining_lentgh_in_state <= 0) {
	  //TODO: implement state change
	}
    _current_sub_state = !_current_sub_state;
    if (_current_sub_state) {
	  _remaining_length_in_sub_state = thresholdrand(_error_free_burst_length + _current_state);
	} else {
	  _remaining_length_in_sub_state = thresholdrand(_error_burst_length + _current_state);
    }
  }
  
  /* Decrease the remaining length in current state/sub-state */
  --_remaining_lentgh_in_state;
  --_remaining_length_in_sub_state;
  
  /* Drop or transmit depending on the sub-state */
  if (_current_sub_state) {
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
EXPORT_ELEMENT(KStateThresholdChannel)
