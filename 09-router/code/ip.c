#include "ip.h"
#include "icmp.h"
#include "rtable.h"
#include "arp.h"
#include "arpcache.h"

#include <stdio.h>
#include <stdlib.h>

// handle ip packet
//
// If the packet is ICMP echo request and the destination IP address is equal to
// the IP address of the iface, send ICMP echo reply; otherwise, forward the
// packet.
void handle_ip_packet(iface_info_t *iface, char *packet, int len)
{
	// fprintf(stderr, "lab09 added: handle ip packet.\n");
	struct iphdr *ip = packet_to_ip_hdr(packet); // 提取目的IP地址
	u32 IP_dest_addr = ntohl(ip->daddr);
	
	if (IP_dest_addr == iface->ip){ // 和当前端口IP相同，是发给本路由器端口的ICMP包
		u8 type = (u8)* (packet + ETHER_HDR_SIZE + IP_HDR_SIZE(ip));
		
		if (type == ICMP_ECHOREQUEST) // ping本端口
			icmp_send_packet(packet, len, ICMP_ECHOREPLY, 0);
		else
			free(packet);
	}
	else{ // 和当前端口IP不同，需要转发
		rt_entry_t *rt = longest_prefix_match(IP_dest_addr); // 最长前缀匹配，查找路由表
		
		if (rt){ // 查找成功
			u8 ttl = ip->ttl;
			ip->ttl = --ttl;
					
			if (ttl <= 0){ // 生存期耗尽
				icmp_send_packet(packet, len, ICMP_TIME_EXCEEDED, ICMP_EXC_TTL);
				return;
			}
			
			ip->checksum = ip_checksum(ip); // 重新计算校验和
			
			if (rt->gw) // 该路由器任何端口IP都与目的IP不在同一网段
				iface_send_packet_by_arp(rt->iface, rt->gw, packet, len);
			else // 下一跳网关为空，说明在同一网段
				iface_send_packet_by_arp(rt->iface, IP_dest_addr, packet, len);
		} 
		else // 查找失败
			icmp_send_packet(packet, len, ICMP_DEST_UNREACH, ICMP_NET_UNREACH);
	}
}
