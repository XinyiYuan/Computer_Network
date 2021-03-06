#include "mospf_daemon.h"
#include "mospf_proto.h"
#include "mospf_nbr.h"
#include "mospf_database.h"

#include "ip.h"
#include "rtable.h"
#include "list.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

extern ustack_t *instance;

pthread_mutex_t mospf_lock;

int idx;

void mospf_init()
{
	pthread_mutex_init(&mospf_lock, NULL);

	instance->area_id = 0;
	// get the ip address of the first interface
	iface_info_t *iface = list_entry(instance->iface_list.next, iface_info_t, list);
	instance->router_id = iface->ip;
	instance->sequence_num = 0;
	instance->lsuint = MOSPF_DEFAULT_LSUINT;

	iface = NULL;
	list_for_each_entry(iface, &instance->iface_list, list) {
		iface->helloint = MOSPF_DEFAULT_HELLOINT;
		init_list_head(&iface->nbr_list);
	}

	init_mospf_db();
}

void *sending_mospf_hello_thread(void *param);
void *sending_mospf_lsu_thread(void *param);
void *checking_nbr_thread(void *param);
void *checking_database_thread(void *param);

void sending_mospf_lsu(void);
void print_mospf_db(void);
void update_rtable(void);
void init_graph(void);
int get_router_list_index(u32 rid);
void Dijkstra(int prev[], int dist[]);
int min_dist(int *dist, int *visited, int num);
void update_router (int prev[], int dist[]);
int find_next_hop(int i, int *prev);
int check_rtable(u32 network, u32 mask);

void mospf_run()
{
	pthread_t hello, lsu, nbr, db;
	pthread_create(&hello, NULL, sending_mospf_hello_thread, NULL);
	pthread_create(&lsu, NULL, sending_mospf_lsu_thread, NULL);
	pthread_create(&nbr, NULL, checking_nbr_thread, NULL);
	pthread_create(&db, NULL, checking_database_thread, NULL);
}

void *sending_mospf_hello_thread(void *param)
{
	// fprintf(stdout, "lab11 added: send mOSPF Hello message periodically.\n");
	
	while (1){
		pthread_mutex_lock(&mospf_lock);
		iface_info_t * iface = NULL;
		
		list_for_each_entry (iface, &instance->iface_list, list) {
			int len = ETHER_HDR_SIZE + IP_BASE_HDR_SIZE + MOSPF_HDR_SIZE + MOSPF_HELLO_SIZE;
			char * packet = (char*)malloc(len);
			bzero(packet, len);
			
			struct ether_header *eh = (struct ether_header *)packet;
			struct iphdr *ip_hdr = packet_to_ip_hdr(packet);
			struct mospf_hdr * mospf_header = (struct mospf_hdr *)((char *)ip_hdr + IP_BASE_HDR_SIZE);
			struct mospf_hello * hello = (struct mospf_hello *)((char *)mospf_header + MOSPF_HDR_SIZE);
			
			eh->ether_type = htons(ETH_P_IP);
			memcpy(eh->ether_shost, iface->mac, ETH_ALEN);
			u8 dhost[ETH_ALEN] = {0x01,0x00,0x5e,0x00,0x00,0x05};
			memcpy(eh->ether_dhost, dhost, ETH_ALEN);
			
			ip_init_hdr(ip_hdr,
				iface->ip,
				MOSPF_ALLSPFRouters,
				len - ETHER_HDR_SIZE,
				IPPROTO_MOSPF);
			
			mospf_init_hdr(mospf_header,
				MOSPF_TYPE_HELLO,
				len - ETHER_HDR_SIZE - IP_BASE_HDR_SIZE,
				instance->router_id,
				0);
			
			mospf_init_hello(hello, iface->mask);
			
			mospf_header->checksum = mospf_checksum(mospf_header);
			iface_send_packet(iface, packet, len);
		}
		pthread_mutex_unlock(&mospf_lock);
		sleep(MOSPF_DEFAULT_HELLOINT);
	}

	return NULL;
}

void *checking_nbr_thread(void *param)
{
	// fprintf(stdout, "lab11 added: neighbor list timeout operation.\n");
	
	while(1){
		iface_info_t *iface = NULL;
		pthread_mutex_lock(&mospf_lock);
		list_for_each_entry (iface, &instance->iface_list, list) {
			mospf_nbr_t * nbr_pos = NULL, *nbr_q;
			list_for_each_entry_safe(nbr_pos, nbr_q, &iface->nbr_list, list) {
				nbr_pos->alive++;
				if (nbr_pos->alive > 3 * iface->helloint) {
					list_delete_entry(&nbr_pos->list);
					free(nbr_pos);
					iface->num_nbr--;
					sending_mospf_lsu();
				}
			}
		}
		
		pthread_mutex_unlock(&mospf_lock);
		sleep(1);
		}

	return NULL;
}

void *checking_database_thread(void *param)
{
	// fprintf(stdout, "lab11 added: link state database timeout operation.\n");
	
	while(1){
		mospf_db_entry_t * db_pos = NULL, *db_q;
		pthread_mutex_lock(&mospf_lock);
		list_for_each_entry_safe(db_pos, db_q, &mospf_db, list) {
			db_pos->alive ++ ;
			if (db_pos->alive > MOSPF_DATABASE_TIMEOUT) {
				list_delete_entry(&db_pos->list);
				free(db_pos);
			}
		}
		pthread_mutex_unlock(&mospf_lock);
		
		//update rtable
		print_mospf_db();
		update_rtable();
		print_rtable();
		sleep(1);
	}

	return NULL;
}

void handle_mospf_hello(iface_info_t *iface, const char *packet, int len)
{
	// fprintf(stdout, "lab11 added: handle mOSPF Hello message.\n");
	
	struct iphdr *ip_hdr = packet_to_ip_hdr(packet);
	struct mospf_hdr * mospf_head = (struct mospf_hdr *)((char*)ip_hdr + IP_HDR_SIZE(ip_hdr));
	struct mospf_hello * hello = (struct mospf_hello *)((char*)mospf_head + MOSPF_HDR_SIZE);
	u32 id = ntohl(mospf_head->rid);
	u32 ip = ntohl(ip_hdr->saddr);
	u32 mask = ntohl(hello->mask);
	int isFound = 0; 
	
	pthread_mutex_lock(&mospf_lock);
	mospf_nbr_t * nbr_pos = NULL;
	list_for_each_entry(nbr_pos, &iface->nbr_list, list) {
		if (nbr_pos->nbr_ip == ip) {
			nbr_pos->alive = 0;
			isFound = 1;
			break;
		}
	}
	if (!isFound) {
		mospf_nbr_t * new_nbr = (mospf_nbr_t *) malloc(sizeof(mospf_nbr_t));
		new_nbr->alive = 0;
		new_nbr->nbr_id = id;
		new_nbr->nbr_ip = ip;
		new_nbr->nbr_mask = mask;
		list_add_tail(&(new_nbr->list), &iface->nbr_list);
		iface->num_nbr++;
		sending_mospf_lsu();
	}
	pthread_mutex_unlock(&mospf_lock);
}

void *sending_mospf_lsu_thread(void *param)
{
	// fprintf(stdout, "lab11 added: send mOSPF LSU message periodically.\n");
	
	while(1){
		pthread_mutex_lock(&mospf_lock);
		sending_mospf_lsu();
		pthread_mutex_unlock(&mospf_lock);
		sleep(MOSPF_DEFAULT_LSUINT);
	}

	return NULL;
}

void handle_mospf_lsu(iface_info_t *iface, char *packet, int len)
{
	// fprintf(stdout, "lab11 added: handle mOSPF LSU message.\n");
	struct iphdr *ip_hdr = packet_to_ip_hdr(packet);
	struct mospf_hdr * mospf_head = (struct mospf_hdr *)((char*)ip_hdr + IP_HDR_SIZE(ip_hdr));
	struct mospf_lsu * lsu = (struct mospf_lsu *)((char*)mospf_head + MOSPF_HDR_SIZE);
	struct mospf_lsa * lsa = (struct mospf_lsa *)((char*)lsu + MOSPF_LSU_SIZE);
	
	pthread_mutex_lock(&mospf_lock);
	u32 rid = ntohl(mospf_head->rid);
	u32 ip = ntohl(ip_hdr->saddr);
	u16 seq = ntohs(lsu->seq);
	u8 ttl = lsu->ttl;
	u32 nadv = ntohl(lsu->nadv);
	int isFound = 0; 
	int isUpdated = 0;
	
	//find db entry
	mospf_db_entry_t * entry_pos = NULL;
	list_for_each_entry (entry_pos, &mospf_db, list) {
		if (entry_pos->rid == rid) {
			// if entry_pos->seq < seq, update db entry
			if (entry_pos->seq < seq) {
				entry_pos->seq = seq;
				entry_pos->nadv = nadv;
				entry_pos->alive = 0;
				for (int i = 0; i < nadv; i++) {
					entry_pos->array[i].mask = ntohl(lsa[i].mask);
					entry_pos->array[i].network = ntohl(lsa[i].network);
					entry_pos->array[i].rid = ntohl(lsa[i].rid);
					fprintf(stdout, "Update db entry "IP_FMT"  "IP_FMT"  "IP_FMT"  "IP_FMT"\n",
						HOST_IP_FMT_STR(entry_pos->rid),
						HOST_IP_FMT_STR(entry_pos->array[i].network),
						HOST_IP_FMT_STR(entry_pos->array[i].mask),
						HOST_IP_FMT_STR(entry_pos->array[i].rid));
				}
				isUpdated = 1;
			}
			isFound = 1;
			break;
		}
	}
	
	//if not found, create a new db_entry
	if (!isFound) {
		entry_pos = (mospf_db_entry_t *)malloc(sizeof(mospf_db_entry_t));
		entry_pos->rid = rid;
		entry_pos->seq = seq;
		entry_pos->nadv = nadv;
		entry_pos->alive = 0;
		
		entry_pos->array = (struct mospf_lsa *)malloc(MOSPF_LSA_SIZE * nadv);
		for (int i = 0; i < nadv; i ++ ) {
			entry_pos->array[i].mask = ntohl(lsa[i].mask);
			entry_pos->array[i].network = ntohl(lsa[i].network);
			entry_pos->array[i].rid = ntohl(lsa[i].rid);
		}
		list_add_tail(&entry_pos->list, &mospf_db);
		isUpdated = 1;
	}
	
	pthread_mutex_unlock(&mospf_lock);
	
	if (isUpdated == 0) {
		return;
	}
	
	//send LSU if ttl > 0
	lsu->ttl -- ;
	if (lsu->ttl > 0) {
		iface_info_t * iface_pos = NULL;
		list_for_each_entry (iface_pos, &instance->iface_list, list) {
			mospf_nbr_t *nbr = NULL;
			list_for_each_entry(nbr, &iface_pos->nbr_list, list) {
				if (nbr->nbr_id == ntohl(mospf_head->rid)) {
					continue;
				}
				char *forwarding_packet = (char *)malloc(len);
				memcpy(forwarding_packet, packet, len);
				
				struct iphdr * iph = packet_to_ip_hdr(forwarding_packet);
				iph->saddr = htonl(iface_pos->ip);
				iph->daddr = htonl(nbr->nbr_ip);
				
				struct mospf_hdr * mospfh = (struct mospf_hdr *)((char *)iph + IP_HDR_SIZE(iph));
				mospfh->checksum = mospf_checksum(mospfh);
				iph->checksum = ip_checksum(iph);
				
				ip_send_packet(forwarding_packet, len);
			}
		}
	}
	
}

void handle_mospf_packet(iface_info_t *iface, char *packet, int len)
{
	struct iphdr *ip = (struct iphdr *)(packet + ETHER_HDR_SIZE);
	struct mospf_hdr *mospf = (struct mospf_hdr *)((char *)ip + IP_HDR_SIZE(ip));

	if (mospf->version != MOSPF_VERSION) {
		log(ERROR, "received mospf packet with incorrect version (%d)", mospf->version);
		return ;
	}
	if (mospf->checksum != mospf_checksum(mospf)) {
		log(ERROR, "received mospf packet with incorrect checksum");
		return ;
	}
	if (ntohl(mospf->aid) != instance->area_id) {
		log(ERROR, "received mospf packet with incorrect area id");
		return ;
	}

	switch (mospf->type) {
		case MOSPF_TYPE_HELLO:
			handle_mospf_hello(iface, packet, len);
			break;
		case MOSPF_TYPE_LSU:
			handle_mospf_lsu(iface, packet, len);
			break;
		default:
			log(ERROR, "received mospf packet with unknown type (%d).", mospf->type);
			break;
	}
}

//lab11 added: send mospf LSU message
void sending_mospf_lsu() {
	iface_info_t * iface = NULL;
	int nbr_num = 0;
	list_for_each_entry (iface, &instance->iface_list, list) { 
		if (iface->num_nbr == 0) {
			nbr_num ++;
		} else {
			nbr_num += iface->num_nbr;
		}	
	}
	
	struct mospf_lsa array [nbr_num + 1];
	bzero(array, (nbr_num + 1) * MOSPF_LSA_SIZE);
	
	int index = 0;
	
	list_for_each_entry (iface, &instance->iface_list, list) {
		if (iface->num_nbr == 0) {
			array[index].mask = htonl(iface->mask);
			array[index].network = htonl(iface->ip & iface->mask);
			array[index].rid = 0;
			index++;
		} else {
			mospf_nbr_t * nbr_pos = NULL;
			list_for_each_entry (nbr_pos, &iface->nbr_list, list) {
				array[index].mask = htonl(nbr_pos->nbr_mask);
				array[index].network = htonl(nbr_pos->nbr_ip & nbr_pos->nbr_mask);
				array[index].rid = htonl(nbr_pos->nbr_id);
				index++;
			}
		}	
	}
	
	instance->sequence_num++;
	int len = ETHER_HDR_SIZE + IP_BASE_HDR_SIZE + MOSPF_HDR_SIZE + MOSPF_LSU_SIZE + nbr_num * MOSPF_LSA_SIZE;
	
	iface = NULL;
	list_for_each_entry (iface, &instance->iface_list, list) { 
		mospf_nbr_t * nbr_pos = NULL;
		list_for_each_entry (nbr_pos, &iface->nbr_list, list) {
			char * packet = (char*)malloc(len);
			bzero(packet, len);
			struct ether_header *eh = (struct ether_header *)packet;
			struct iphdr *ip_hdr = packet_to_ip_hdr(packet);
			struct mospf_hdr * mospf_header = (struct mospf_hdr *)((char*)ip_hdr + IP_BASE_HDR_SIZE);
			struct mospf_lsu * lsu = (struct mospf_lsu *)((char*)mospf_header + MOSPF_HDR_SIZE);
			
			eh->ether_type = htons(ETH_P_IP);
			memcpy(eh->ether_shost, iface->mac, ETH_ALEN);
			
			ip_init_hdr(ip_hdr,
				iface->ip,
				nbr_pos->nbr_ip,
				len - ETHER_HDR_SIZE,
				IPPROTO_MOSPF);
			
			mospf_init_hdr(mospf_header,
				MOSPF_TYPE_LSU,
				len - ETHER_HDR_SIZE - IP_BASE_HDR_SIZE,
				instance->router_id,
				instance->area_id);
			
			mospf_init_lsu(lsu, nbr_num);
			memcpy(packet + ETHER_HDR_SIZE + IP_BASE_HDR_SIZE + MOSPF_HDR_SIZE + MOSPF_LSU_SIZE, array, nbr_num * MOSPF_LSA_SIZE);
			
			mospf_header->checksum = mospf_checksum(mospf_header);
			ip_send_packet(packet, len);
		}
	}	
}

//lab11 added: print mospf database
void print_mospf_db() {
	mospf_db_entry_t * entry_pos = NULL;
	printf("RID\tNetwork\tMask\tNeighbor\n");
	list_for_each_entry(entry_pos, &mospf_db, list){
		for(int i = 0;i < entry_pos->nadv; i++) {
			fprintf(stdout, IP_FMT"\t"IP_FMT"\t"IP_FMT"\t"IP_FMT"\n",
				HOST_IP_FMT_STR(entry_pos->rid),
				HOST_IP_FMT_STR(entry_pos->array[i].network), 
				HOST_IP_FMT_STR(entry_pos->array[i].mask),
				HOST_IP_FMT_STR(entry_pos->array[i].rid)
			);
		}
	}
}

void update_rtable() {
	int prev[ROUTER_NUM];
	int dist[ROUTER_NUM];
	init_graph();
	Dijkstra(prev, dist);
	update_router(prev, dist);
}

void init_graph() {
	memset(graph, INT8_MAX -1, sizeof(graph));
	mospf_db_entry_t *db_entry = NULL;
	router_list[0] = instance->router_id;
	idx = 1;
	
	list_for_each_entry(db_entry, &mospf_db, list) {
		router_list[idx] = db_entry->rid;
		idx++;
	}
	
	db_entry = NULL;
	list_for_each_entry(db_entry, &mospf_db, list) {
		int u = get_router_list_index(db_entry->rid);
		for(int i = 0; i < db_entry->nadv; i ++ ) {
			if(!db_entry->array[i].rid) {
				continue;
			} 
			int v = get_router_list_index(db_entry->array[i].rid);
			graph[u][v] = graph[v][u] = 1;
		}
	}
}

int get_router_list_index(u32 rid) {
	for(int i = 0; i < idx; i ++ )
		if(router_list[i] == rid) {
			return i;
		} 
	return -1;
}

void Dijkstra(int prev[], int dist[]) {
	int visited[ROUTER_NUM];
	for(int i = 0; i < ROUTER_NUM; i++) {
		prev[i] = -1;
		dist[i] = INT8_MAX;
		visited[i] = 0;
	}
	
	dist[0] = 0;
	
	for(int i = 0; i < idx; i++) {
		int u = min_dist(dist, visited, idx);
		visited[u] = 1;
		for (int v = 0; v < idx; v++){
			if (visited[v] == 0 && dist[u] + graph[u][v] < dist[v]) {
				dist[v] = dist[u] + graph[u][v];
				prev[v] = u;
			}
		}
	}
	
}

int min_dist(int *dist, int *visited, int num) {
	int index = -1;
	for (int u = 0; u < num; u++) {
		if (visited[u]) {
			continue;
		}
		if (index == -1 || dist[u] < dist[index]) {
			index = u;
		}
		
	}
	return index;
}

void update_router (int prev[], int dist[]) {
	int visited[ROUTER_NUM];
	memset(visited, 0, sizeof(visited));
	visited[0] = 1; 
	
	rt_entry_t *rt_entry, *q;
	list_for_each_entry_safe (rt_entry, q, &rtable, list) {
		if(rt_entry->gw) {
			remove_rt_entry(rt_entry);
		} 
	}
	
	for (int i = 0; i < idx; i ++ ) {
		int t = -1;
		for(int j = 0; j < idx; j ++ ) {
			if(visited[j]) {
				continue;
			}
			if(t == -1 || dist[j] < dist[t]){
				t = j;
			}
		}
		visited[t] = 1;
		
		mospf_db_entry_t *db_entry;
		list_for_each_entry(db_entry, &mospf_db, list) {
			if(db_entry->rid == router_list[t]) {
				int next_hop_id = find_next_hop(t, prev);
				iface_info_t *iface;
				u32 gw;
				int isFound = 0;
				list_for_each_entry (iface, &instance->iface_list, list) {
					mospf_nbr_t *nbr_pos;
					list_for_each_entry (nbr_pos, &iface->nbr_list, list) {
						if(nbr_pos->nbr_id == router_list[next_hop_id]) {
							isFound = 1;
							gw = nbr_pos->nbr_ip;
							break;
						}
					}
					if(isFound){
						break;
					} 
				}
				if(!isFound){
					break;
				} 
				for(int i = 0; i < db_entry->nadv; i ++) {
					u32 network = db_entry->array[i].network;
					u32 mask = db_entry->array[i].mask;
					int isExist = check_rtable(network, mask);
					if(!isExist) {
						rt_entry_t *new_entry = new_rt_entry(network, mask, gw, iface);
						add_rt_entry(new_entry);
					}
				}
			}
		}
	}
}

int find_next_hop(int i, int *prev) {
	while(prev[i] != 0) {
		i = prev[i];
	}
	return i;
}

int check_rtable(u32 network, u32 mask) {
	rt_entry_t *rt_entry = NULL;
	list_for_each_entry(rt_entry, &rtable, list) {
		if(rt_entry->dest == network && rt_entry->mask == mask) {
			return 1;
		} 
	}
	return 0;
}