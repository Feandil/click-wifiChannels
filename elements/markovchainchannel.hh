#ifndef CLICK_MARKOVCHAINCHANNEL_HH
#define CLICK_MARKOVCHAINCHANNEL_HH
#include <click/element.hh>
#include <click/fromfile.hh>
#include <click/vector.hh>
CLICK_DECLS

class MarkovChainChannel : public Element {

  private:
    /* Variables used to store the statistic representation from the configuration files */
    Vector<uint32_t> _success_probablilty;
  
    /* FileDescriptor */
    FromFile _ff;
   
    /* Current state description */
    uint32_t _current_state;
    uint32_t _state_modulo;
 
  public:
    /* Behaviour descriptors */
    const char *class_name() const { return "MarkovChainChannel"; }
    const char *port_count() const { return PORTS_1_1X2; }
    const char *processing() const { return PUSH; }
    const char *flow_code()  const { return COMPLETE_FLOW; }

    /* Static initializer : called only once by Click */
    void static_initialize();

    /* Configure the Element */
    int configure(Vector<String> &, ErrorHandler *);

    /* Initialize/cleanup the Element, called after the configure */
    int initialize (ErrorHandler *errh);
    void cleanup(CleanupStage stage);

    /* receive packet from above */
    void push (int, Packet *);
};

CLICK_ENDDECLS
#endif
