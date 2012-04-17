#ifndef CLICK_KSTATETHRESHOLDCHANNEL_HH
#define CLICK_KSTATETHRESHOLDCHANNEL_HH
#include <click/element.hh>
#include <click/fromfile.hh>
CLICK_DECLS

class KStateThresholdChannel : public Element {

  private:

    /* Structures used to hold the Cumulative distribution functions */
    struct cdfPoint {
      double probability;
	  int    point;
    };
	typedef struct cdfPoint cdfPoint_t;
    struct cdf {
      int len;
	  cdfPoint_t* points;
    };
	typedef struct cdf cdf_t;
  
    /* Variables used to store the cdf from the configuration files */
    cdf_t* _error_free_burst_length;
    cdf_t* _error_burst_length;
  
    /* FileDescriptor */
    FromFile _ff;
	
	/* Load a CDF for a file */
	cdf_t* load_cdf_from_file(String&, ErrorHandler*);

    /* Generate a random number from a Cumulative distribution functions */
    int thresholdrand(const cdf_t*);
 
    /* Number of states */
    int _nbstates;
  
    /* Current state/substate description */
    int _current_state;
    int _remaining_lentgh_in_state;
    bool _current_sub_state;  // True if error-free, false if error
    int _remaining_length_in_sub_state;
 
  public:

    /* Void constructors */
    KStateThresholdChannel();
    ~KStateThresholdChannel();

    /* Behaviour descriptors */
    const char *class_name() const { return "KStateThresholdChannel"; }
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
