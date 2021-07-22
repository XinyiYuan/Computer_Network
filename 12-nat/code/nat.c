#include "nat.h"
#include "ip.h"
#include "icmp.h"
#include "tcp.h"
#include "rtable.h"
#include "log.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#define MAX_LINE 100

static struct nat_table nat;

char internal_iface_des [MAX_LINE] = "internal-iface";
char external_iface_des [MAX_LINE] = "external-iface";
char dnat_rules_des [MAX_LINE] = "dnat-rules";

// get the interface from iface name
static iface_info_t *if_name_to_iface(const char *if_name)
{
	iface_info_t *iface = NULL;
	list_for_each_entry(iface, &instance->iface_list, list) {
		if (strcmp(iface->name, if_name) == 0)
			return iface;
	}

	log(ERROR, "Could not find the desired interface according to if_name '%s'", if_name);
	return NULL;
}

// determine the direction of the packet, DIR_IN / DIR_OUT / DIR_INVALID
static int get_packet_direction(char *packet)
{
	// fprintf(stdout, "lab12 added: determine the direction of this packet.\n");
	
	struct iphdr *ip = packet_to_ip_hdr(packet);
	u32 saddr = ntohl(ip->saddr);
	rt_entry_t *rt = longest_prefix_match(saddr);
	iface_info_t *iface = rt->iface;
	
	if (iface->index == nat.internal_iface->index)
		return DIR_OUT;
	else if (iface->index == nat.external_iface->index)
		return DIR_IN;

	return DIR_INVALID;
}

// do translation for the packet: replace the ip/port, recalculate ip & tcp
// checksum, update the statistics of the tcp connection
void do_translation(iface_info_t *iface, char *packet, int len, int dir)
{
	// fprintf(stdout, "lab12 added: do translation for this packet.\n");
	
	pthread_mutex_lock(&nat.lock);
	
	struct iphdr *ip = packet_to_ip_hdr(packet);
	struct tcphdr *tcp = packet_to_tcp_hdr(packet);
	u32 hash_address = (dir == DIR_IN)? ntohl(ip->saddr) : ntohl(ip->daddr);
	u8 hash_i = hash8((char*)&hash_address, 4);
	// fprintf(stdout,"nat mapping table of hash_value %d\n",hash_i);
	struct list_head *head = &(nat.nat_mapping_list[hash_i]);
	struct nat_mapping *mapping_entry = NULL;
	
	int found = 0;
	
	/*
	list_for_each_entry(mapping_entry, head, list){
		fprintf(stdout, "%x\n",mapping_entry->external_ip);
		fprintf(stdout, "%x\n",mapping_entry->external_port);
	}
	*/
	
	if (dir == DIR_IN){
		found = 0;
		
		list_for_each_entry(mapping_entry, head, list) {
			if (mapping_entry->external_ip == ntohl(ip->daddr) && mapping_entry->external_port == ntohs(tcp->dport)){
				found = 1;
				break;
			}
		}
		
		if (!found){
			struct nat_mapping *new_entry = (struct nat_mapping*)malloc(sizeof(struct nat_mapping));
			memset(new_entry, 0, sizeof(struct nat_mapping));
			new_entry->external_ip = ntohl(ip->daddr);
			new_entry->external_port = ntohs(tcp->dport);
			
			list_add_tail(&(new_entry->list),head);
			mapping_entry = new_entry;
		}
		
		tcp->dport = htons(mapping_entry->internal_port);
		ip->daddr = htonl(mapping_entry->internal_ip);
		mapping_entry->conn.external_fin = (tcp->flags == TCP_FIN);
		mapping_entry->conn.external_seq_end = tcp->seq;
		
		if (tcp->flags == TCP_ACK)
			mapping_entry->conn.external_ack = tcp->ack;
	}
	else{ 
		found = 0;
		
		list_for_each_entry(mapping_entry, head, list){
			if (mapping_entry->internal_ip == ntohl(ip->saddr) && mapping_entry->internal_port == ntohs(tcp->sport)){
				found = 1;
				break;
			}
		}
		
		if (!found) {
			struct nat_mapping *new_entry = (struct nat_mapping*)malloc(sizeof(struct nat_mapping));
			memset(new_entry, 0, sizeof(struct nat_mapping));
			new_entry->internal_ip = ntohl(ip->saddr);
			new_entry->internal_port = ntohs(tcp->sport);
			new_entry->external_ip = nat.external_iface->ip;
			u16 i;
			
			for (i = NAT_PORT_MIN; i < NAT_PORT_MAX; i++)
				if (!nat.assigned_ports[i]){
					nat.assigned_ports[i] = 1;
					break;
				}
		
			new_entry->external_port = i;
			list_add_tail(&(new_entry->list),head);
			mapping_entry = new_entry;
		}
		
		tcp->sport = htons(mapping_entry->external_port);
		ip->saddr = htonl(mapping_entry->external_ip);
		mapping_entry->conn.internal_fin = (tcp->flags == TCP_FIN);
		mapping_entry->conn.internal_seq_end = tcp->seq;
	
		if (tcp->flags == TCP_ACK)
			mapping_entry->conn.internal_ack = tcp->ack;
	}
	
	tcp->checksum = tcp_checksum(ip, tcp);
	ip->checksum = ip_checksum(ip);
	mapping_entry->update_time = time(NULL);
	
	pthread_mutex_unlock(&nat.lock);
	
	ip_send_packet(packet, len);
}

void nat_translate_packet(iface_info_t *iface, char *packet, int len)
{
	int dir = get_packet_direction(packet);
	if (dir == DIR_INVALID) {
		log(ERROR, "invalid packet direction, drop it.");
		icmp_send_packet(packet, len, ICMP_DEST_UNREACH, ICMP_HOST_UNREACH);
		free(packet);
		return ;
	}

	struct iphdr *ip = packet_to_ip_hdr(packet);
	if (ip->protocol != IPPROTO_TCP) {
		log(ERROR, "received non-TCP packet (0x%0hhx), drop it", ip->protocol);
		free(packet);
		return ;
	}

	do_translation(iface, packet, len, dir);
}

// check whether the flow is finished according to FIN bit and sequence number
// XXX: seq_end is calculated by `tcp_seq_end` in tcp.h
static int is_flow_finished(struct nat_connection *conn)
{
    return (conn->internal_fin && conn->external_fin) && \
            (conn->internal_ack >= conn->external_seq_end) && \
            (conn->external_ack >= conn->internal_seq_end);
}

// nat timeout thread: find the finished flows, remove them and free port
// resource
void *nat_timeout()
{
	while (1) {
		// fprintf(stdout, "lab12 added: sweep finished flows periodically.\n");
		pthread_mutex_lock(&nat.lock);
		
		time_t now = time(NULL);
		for (int i = 0; i < HASH_8BITS; i++){
			struct list_head *head = &(nat.nat_mapping_list[i]);
			if (!list_empty(head)){
				struct nat_mapping *mapping, *temp;
				
				list_for_each_entry_safe(mapping, temp, head, list){
					if (now - mapping->update_time > TCP_ESTABLISHED_TIMEOUT){
						nat.assigned_ports[mapping->external_port] = 0;
						list_delete_entry(&mapping->list);
						free(mapping);
						continue;
					}
					
					//struct nat_connection *conn = mapping->conn;				
					if (is_flow_finished(&(mapping->conn))){
						// fprintf(stdout,"delete!");
						nat.assigned_ports[mapping->external_port] = 0;
						list_delete_entry(&mapping->list);
						free(mapping);
					}
				}
				
			}
		}
		
		pthread_mutex_unlock(&nat.lock);
		sleep(1);
	}

	return NULL;
}


void parse_internal_iface(char * line) {
	char * des;
	char * iface_name;
	char * net = strtok(line, ": ");
	iface_name = strtok(NULL, " ");
	iface_name[strlen(iface_name)-1] = '\0';
	iface_info_t * iface = if_name_to_iface(iface_name);
	nat.internal_iface = iface;
}

void parse_external_iface(char * line) {
	char * des;
	char * iface_name;
	char * net = strtok(line, ": ");
	iface_name = strtok(NULL, " ");
	iface_name[strlen(iface_name)-1] = '\0';
	iface_info_t * iface = if_name_to_iface(iface_name);
	nat.external_iface = iface;
}

int net_parser(char * s) {
	unsigned int num = 0;
	int i = 0;
	int time = 0;
	while(time < 3) {
		num = num + atoi(&s[i]);
		num = num * 256;
		for (; i < strlen(s) && s[i] != '.'; i++) {
		}
		i++;
		time ++;
	}
	num = num + atoi(&s[i]);
	return num;
}

void parse_dnat_rules(char * line) {
	char * des;
	char * ip1, * ip2;
	char * net = strtok(line, ": ");
	ip1 = strtok(NULL, " ");
	ip2 = strtok(NULL, "-> ");
	ip1 = strtok(ip1, ":");
	char * port1 = strtok(NULL, " ");
	ip2 = strtok(ip2, ":");
	char * port2 = strtok(NULL, " ");
	
	int external_ip = net_parser(ip1);
	int external_port = atoi(port1);
	int internal_ip = net_parser(ip2);
	int internal_port = atoi(port2);
	
	struct nat_mapping * mapping_entry = (struct nat_mapping *)malloc(sizeof(struct nat_mapping));
	bzero(mapping_entry, sizeof(struct nat_mapping));
	mapping_entry->external_ip = external_ip;
	mapping_entry->internal_ip = internal_ip;
	mapping_entry->external_port = (u16)external_port;
	mapping_entry->internal_port = (u16)internal_port;
	mapping_entry->update_time = time(NULL);
	
	struct dnat_rule * rule = (struct dnat_rule *)malloc(sizeof(struct dnat_rule));
	rule->external_ip = external_ip;
	rule->internal_ip = internal_ip;
	rule->external_port = (u16)external_port;
	rule->internal_port = (u16)internal_port;
	list_add_tail(&rule->list ,&nat.rules);
}

int parse_config(const char *filename)
{
	// fprintf(stdout, "lab12 added: parse config file, including i-iface, e-iface (and dnat-rules if existing).\n");
	FILE * fd = fopen(filename, "r");
	if (fd == NULL) {
		return 0;
	}
	char * line = (char *)malloc(MAX_LINE);
	while (fgets(line, MAX_LINE, fd) != NULL) {
		if (strstr(line, internal_iface_des)) {
			parse_internal_iface(line);
		} else if (strstr(line, external_iface_des)) {
			parse_external_iface(line);
		} else if (strstr(line, dnat_rules_des)) {
			parse_dnat_rules(line);
		}
	}
	
	return 0;
}

// initialize
void nat_init(const char *config_file)
{
	memset(&nat, 0, sizeof(nat));

	for (int i = 0; i < HASH_8BITS; i++)
		init_list_head(&nat.nat_mapping_list[i]);

	init_list_head(&nat.rules);

	// seems unnecessary
	memset(nat.assigned_ports, 0, sizeof(nat.assigned_ports));

	parse_config(config_file);

	pthread_mutex_init(&nat.lock, NULL);

	pthread_create(&nat.thread, NULL, nat_timeout, NULL);
}

void nat_exit()
{
	// fprintf(stdout, "lab12 added: release all resources allocated.\n");
	pthread_mutex_lock(&nat.lock);
	
	for (int i = 0; i < HASH_8BITS; i++){
		struct list_head *head = &nat.nat_mapping_list[i];
		struct nat_mapping *mapping_entry, *temp;
		
		list_for_each_entry_safe(mapping_entry, temp, head, list){
			list_delete_entry(&mapping_entry->list);
			free(mapping_entry);
		}
	}
	pthread_kill(nat.thread, SIGTERM);
	
	pthread_mutex_unlock(&nat.lock);
}
