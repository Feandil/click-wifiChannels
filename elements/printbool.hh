#ifndef CLICK_PRINTBOOL_HH
#define CLICK_PRINTBOOL_HH
#include <click/element.hh>
CLICK_DECLS

class PrintBool : public Element {
  public:
    /* Behaviour descriptors */
    const char *class_name() const { return "PrintBool"; } // Name of this thing
    const char *port_count() const { return "2/0"; }       // 2 ports in, 0 port out
    const char *processing() const { return PUSH; }        // Working in push mode (not pull nor agnostic)

    /* Configure the Element */
    int configure(Vector<String> &, ErrorHandler *) { return 0; }

    /* receive packet from above */
    void push (int, Packet *);
};

CLICK_ENDDECLS
#endif
