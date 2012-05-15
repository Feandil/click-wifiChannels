#include <click/config.h>
#include <click/args.hh>
#include <click/error.hh>
#include <click/glue.hh>
#include <click/confparse.hh>

#include "markovchainchannel.hh"

CLICK_DECLS
void
MarkovChainChannel::static_initialize()
{
  //Probably not needed:
  click_random_srandom();
}

int
MarkovChainChannel::configure(Vector<String> &conf, ErrorHandler *errh)
{
#if CLICK_USERLEVEL || CLICK_TOOL
  if (Args(conf, this, errh)
      .read_m("FILENAME", FilenameArg(), _ff.filename())
      .complete() < 0) {
    return -1;
  }
#else
  if (Args(conf, this, errh)
      .read_m("FILENAME", _ff.filename())
      .complete() < 0) {
    return -1;
  }
#endif
  return 0;
}

int
MarkovChainChannel::initialize(ErrorHandler *errh)
{
  String in;
  uint32_t buffer;
  uint32_t len;
  
  if (_ff.initialize(errh) < 0) {
    errh->error("MarkovChain input file unreadable");
    return -1;
  }
  
  if ((_ff.read_line(in, errh) <= 0) || (cp_integer(in.begin(), in.end() - 1, 10, &len) != in.end() - 1)) {
    errh->error("MarkovChain input file error : bad input (reading length)");
    _ff.cleanup();
    return -2;
  }
  
  _success_probablilty.reserve(len);
  _state_modulo = len;

  if ((_ff.read_line(in, errh) <= 0) || (cp_integer(in.begin(), in.end() - 1, 10, &_current_state) != in.end() - 1)) {
    errh->error("MarkovChain input file error : bad input (reading initial state)");
    _ff.cleanup();
    return -2;
  }
  _current_state %= _state_modulo;

  while (len != 0) {
    --len;
    if ((_ff.read_line(in, errh) <= 0) || (cp_integer(in.begin(), in.end() - 1, 10, &buffer) != in.end() - 1)) {
       errh->error("MarkovChain input file error : bad input");
      _ff.cleanup();
      _success_probablilty.clear();
      return -3;
    }
    _success_probablilty.push_back(buffer);
  }
  _ff.cleanup();
  return 0;
}

void
MarkovChainChannel::cleanup(CleanupStage)
{
  _success_probablilty.clear();
}

void
MarkovChainChannel::push (int, Packet *p)
{
  
  /* Evaluate the transmission */
  bool transmit = click_random() < _success_probablilty[_current_state];
  
  /* Update the state */
  _current_state = ((_current_state << 1) + transmit) % _state_modulo;
  
  /* Drop or transmit */
  if (transmit) {
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
EXPORT_ELEMENT(MarkovChainChannel)
