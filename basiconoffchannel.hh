#ifndef CLICK_BASICONOFFCHANNEL_HH
#define CLICK_BASICONOFFCHANNEL_HH
#include <click/element.hh>
#include <click/fromfile.hh>
CLICK_DECLS

class BasicOnOffChannel : public Element {

  private:

    /* Structures used to hold the Cumulative distribution functions */
    struct cdfPoint {
      uint32_t probability;
      int point;
    };
	
	typedef struct cdfPoint cdfPoint_t;
    struct cdf {
      uint32_t len;
      cdfPoint_t *points;
    };
	typedef struct cdf cdf_t;
  
    /* Variables used to store the statistic representation from the configuration files */
    uint32_t _initial_error_probability;
    cdf_t *_error_burst_length;
    cdf_t *_error_free_burst_length;
  
    /* FileDescriptor */
    FromFile _ff;
    String _error_cdf_filename;
    String _error_free_cdf_filename;
	
	  /* Load a CDF for a file */
    int load_cdf_from_file(const String, ErrorHandler *, cdf_t **);

    /* Generate a random number from a Cumulative distribution functions */
    int thresholdrand(const cdf_t *);
   
    /* Current state description */

    bool _current_state;  // True if error-free, false if error
    int _remaining_length_in_state;
 
  public:

    /* Void constructors */
    BasicOnOffChannel();
    ~BasicOnOffChannel();

    /* Behaviour descriptors */
    const char *class_name() const { return "BasicOnOffChannel"; }
    const char *port_count() const { return PORTS_1_1X2; }
    const char *processing() const { return PUSH; }
    const char *flow_code()  const { return COMPLETE_FLOW; }

    /* Static initializer/cleanup : called only once by Click */
    void static_initialize();
    void static_cleanup();

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
