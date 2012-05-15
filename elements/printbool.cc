#include <click/config.h>
#include <click/args.hh>
#include <click/error.hh>
#include <click/glue.hh>

#include "printbool.hh"

CLICK_DECLS

void
PrintBool::push (int port, Packet *p)
{
  click_chatter("%u",!port);
  p->kill();
}

CLICK_ENDDECLS
EXPORT_ELEMENT(PrintBool)
