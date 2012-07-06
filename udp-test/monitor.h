#ifndef MONITOR_H
#define MONITOR_H

int open_monitor_interface(const char *interface, const int phy_inter);
int close_interface(const char *interface);

#endif /* MONITOR_H */
