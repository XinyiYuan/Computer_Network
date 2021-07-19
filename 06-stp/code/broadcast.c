#include "base.h"
#include <stdio.h>

// XXX ifaces are stored in instace->iface_list
extern ustack_t *instance;

extern void iface_send_packet(iface_info_t *iface, const char *packet, int len);

void broadcast_packet(iface_info_t *iface, const char *packet, int len)
{
	// lab04 added: broadcast packet 
	iface_info_t *IFACE = NULL;
	
	list_for_each_entry(IFACE, &instance->iface_list,list){
		if(IFACE->fd != iface->fd)
			iface_send_packet(IFACE, packet, len);
	}
	
	fprintf(stdout, "NOTICE: Broadcast Complete.\n");
}
