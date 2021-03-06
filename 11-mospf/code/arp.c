#include "arp.h"
#include "base.h"
#include "types.h"
#include "ether.h"
#include "arpcache.h"

#include <stdlib.h>
#include <string.h>

#include "log.h"

// send an arp request: encapsulate an arp request packet, send it out through
// iface_send_packet
void arp_send_request(iface_info_t *iface, u32 dst_ip)
{
	int len = ETHER_HDR_SIZE + sizeof(struct ether_arp);
	char *packet = malloc(len);
	if (!packet) {
		log(ERROR, "malloc arp request failed.");
		return ;
	}

	// init arp request 
	struct ether_arp *arp = packet_to_ether_arp(packet);
	memset(arp, 0, sizeof(struct ether_arp));
	arp->arp_hrd = htons(ARPHRD_ETHER);
	arp->arp_pro = htons(ETH_P_IP);
	arp->arp_hln = ETH_ALEN;
	arp->arp_pln = sizeof(u32);
	arp->arp_op = htons(ARPOP_REQUEST);
	memcpy(arp->arp_sha, &iface->mac, ETH_ALEN);
	arp->arp_spa = htonl(iface->ip);
	arp->arp_tpa = htonl(dst_ip);

	// init ether header
	static u8 bc_mac[ETH_ALEN] = { 255, 255, 255, 255, 255, 255 };
	struct ether_header *eh = (struct ether_header *)packet;
	memcpy(eh->ether_dhost, bc_mac, ETH_ALEN);
	memcpy(eh->ether_shost, &iface->mac, ETH_ALEN);
	eh->ether_type = htons(ETH_P_ARP);

	iface_send_packet(iface, packet, len);
}

// send an arp reply packet: encapsulate an arp reply packet, send it out
// through iface_send_packet
void arp_send_reply(iface_info_t *iface, struct ether_arp *req_hdr)
{
	int len = ETHER_HDR_SIZE + sizeof(struct ether_arp);
	char *packet = malloc(len);
	if (!packet) {
		log(ERROR, "malloc arp request failed.");
		return ;
	}

	struct ether_arp *arp = (struct ether_arp *)(packet + ETHER_HDR_SIZE);
	memset(arp, 0, sizeof(struct ether_arp));

	arp->arp_hrd = htons(ARPHRD_ETHER);
	arp->arp_pro = htons(ETH_P_IP);
	arp->arp_hln = ETH_ALEN;
	arp->arp_pln = sizeof(u32);
	arp->arp_op = htons(ARPOP_REPLY);

	memcpy(arp->arp_sha, &iface->mac, ETH_ALEN);
	arp->arp_spa = htonl(iface->ip);
	memcpy(arp->arp_tha, req_hdr->arp_sha, ETH_ALEN);
	arp->arp_tpa = req_hdr->arp_spa;

	struct ether_header *eh = (struct ether_header *)packet;
	memcpy(eh->ether_dhost, req_hdr->arp_sha, ETH_ALEN);
	memcpy(eh->ether_shost, &iface->mac, ETH_ALEN);
	eh->ether_type = htons(ETH_P_ARP);

	iface_send_packet(iface, packet, len);
}

// handle arp packet: 
//
// 1. If the dest ip address of this arp packet is not equal to the ip address 
//    of the incoming iface, drop it. 
// 2. If it is an arp request packet, send arp reply to the destination, insert 
//    the ip->mac mapping into arpcache.
// 3. If it is an arp reply packet, insert the ip->mac mapping into arpcache.
void handle_arp_packet(iface_info_t *iface, char *packet, int len)
{
	struct ether_arp *arp = packet_to_ether_arp(packet);
	u32 src_ip = ntohl(arp->arp_spa),
		dst_ip = ntohl(arp->arp_tpa);

	if (iface->ip == dst_ip) {
		if (ntohs(arp->arp_op) == ARPOP_REQUEST) {
			arp_send_reply(iface, arp);
			arpcache_insert(src_ip, arp->arp_sha);
		} else if (ntohs(arp->arp_op) == ARPOP_REPLY) {
			// cache this arp entry.
			arpcache_insert(src_ip, arp->arp_sha);
		}
	}

	free(packet);
}

// send (IP) packet through arpcache lookup 
//
// Lookup the mac address of dst_ip in arpcache. If it is found, fill the
// ethernet header and emit the packet by iface_send_packet, otherwise, pending 
// this packet into arpcache, and send arp request.
void iface_send_packet_by_arp(iface_info_t *iface, u32 dst_ip, char *packet, int len)
{
	struct ether_header *eh = (struct ether_header *)packet;
	memcpy(eh->ether_shost, iface->mac, ETH_ALEN);
	eh->ether_type = htons(ETH_P_IP);

	u8 dst_mac[ETH_ALEN];
	int found = arpcache_lookup(dst_ip, dst_mac);
	if (found) {
		memcpy(eh->ether_dhost, dst_mac, ETH_ALEN);
		iface_send_packet(iface, packet, len);
	}
	else {
		arpcache_append_packet(iface, dst_ip, packet, len);
	}
}
