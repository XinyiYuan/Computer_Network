#include "icmp.h"
#include "ip.h"
#include "rtable.h"
#include "arp.h"
#include "base.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// send icmp packet
void icmp_send_packet(const char *in_pkt, int len, u8 type, u8 code)
{
	// fprintf(stderr, "lab09 added: malloc and send icmp packet.\n");
	
	struct iphdr *in_pkt_IPhead = packet_to_ip_hdr(in_pkt); // in_pkt的IP首部
	int pkt_len = 0;
	
	if (type == ICMP_ECHOREPLY) // 回复ping
		pkt_len = len;
	else
		pkt_len = ETHER_HDR_SIZE + IP_BASE_HDR_SIZE + IP_HDR_SIZE(in_pkt_IPhead) + ICMP_HDR_SIZE + 8;
	
	char *packet = (char*)malloc(pkt_len * sizeof(char));
	
	struct ether_header *ehdr = (struct ether_header*)packet; // ether head
	ehdr->ether_type = htons(ETH_P_IP);
	
	struct iphdr *packet_IPhead = packet_to_ip_hdr(packet); // packet的IP首部
	
	rt_entry_t *rt = longest_prefix_match(ntohl(in_pkt_IPhead->saddr));
	
	ip_init_hdr(packet_IPhead, rt->iface->ip, ntohl(in_pkt_IPhead->saddr), pkt_len - ETHER_HDR_SIZE, 1);
	
	struct icmphdr *packet_IChead = (struct icmphdr*)(packet + ETHER_HDR_SIZE + IP_BASE_HDR_SIZE); // packet的ICMP首部
	packet_IChead->type = type;
	packet_IChead->code = code;
	
	int packet_Rest_begin = ETHER_HDR_SIZE + IP_HDR_SIZE(packet_IPhead) + 4;
	
	if (type == ICMP_ECHOREPLY)
		memcpy(packet + packet_Rest_begin, in_pkt + packet_Rest_begin, pkt_len - packet_Rest_begin);
	else {
		memset(packet + packet_Rest_begin, 0, 4); // 开头4位设置为0
		memcpy(packet + packet_Rest_begin + 4, in_pkt + ETHER_HDR_SIZE, IP_HDR_SIZE(in_pkt_IPhead) + 8);
	}
	
	packet_IChead->checksum = icmp_checksum(packet_IChead, pkt_len - ETHER_HDR_SIZE - IP_BASE_HDR_SIZE);
	ip_send_packet(packet, pkt_len);
}
