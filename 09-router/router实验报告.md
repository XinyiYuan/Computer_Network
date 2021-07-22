# 路由器转发实验

<center> 中国科学院大学 </center>

<center>袁欣怡 2018K8009929021</center>

<center> 2021.5.11 </center>

## 实验内容

---

1. 完成 `main.c` 中调用的 `handle_arp_packet`、`handle_ip_packet` 等函数，实现路由器功能，具体包括：处理 `ARP` 请求和应答， `ARP` 缓存管理， `IP` 地址查找， `IP` 数据包转发和发送 `ICMP` 数据包等。
2. 运行 `router_topo.py` ，在 `h1` 结点 `ping` 其他结点，检查是否能 `ping` 通、 `router` 功能是否完成。
3. 自己构造包含多个路由器结点的网络，手动配置各个结点的路由表，并在两个终端结点之间进行连通性测试。



## 实验流程

---

### 1. 搭建实验环境

本实验中涉及到的文件主要有：

`main.c`：路由器代码，编译后生成可执行文件router

`arp.c`：需要实现的函数有：（1）`arp_send_request`：发送 `ARP` 请求；（2）`arp_send_reply`：进行 `ARP` 应答；（3）`handle_arp_packet`：处理 `ARP` 数据包。

`arpcache.c`：需要实现函数有：（1）`arpcache_lookup`：查找 `ARP cache` 中是否有对应的 `IP` 和 `mac` 地址；（2）`arpcache_append_packet`：查询 `ARP cache` 失败时，将包挂起并发送 `ARP` 请求；（3）`arpcache_insert`：写 `IP` 到 `mac` 的映射，并发送等待该映射的数据包；（4）`arpcache_sweep`：删除已经不具备时效性的表项，处理未收到应答的包。

`icmp.c`：需要实现的函数有：（1）`icmp_send_packet`：发送 `ICMP` 数据包。

`ip.c`：需要实现的函数有：（1）`handle_ip_packet`：处理 `IP` 数据包，根据情况判断需要回应 `ICMP` 报文还是转发数据包。

`ip_base.c`：需要实现的函数有：（1）`longest_prefix_match`：最长前缀查找；（2）`ip_send_packet`：发送IP数据包。

`router_topo.py`：构建包含1个 `router` 、3个 `host` 的网络拓扑结构

<img src="/Users/smx1228/Desktop/IMG_5080.jpg" alt="IMG_5080" style="zoom:15%;" />

`my_topo.py`：构建包含2个 `router` 、2个 `host` 的网络拓扑结构

<img src="/Users/smx1228/Desktop/IMG_5081.jpg" alt="IMG_5081" style="zoom:27%;" />



### 2. 实验代码设计

首先我们需要知道，路由器在收到一个包时的处理流程。

第一步，路由器会读这个包的头部，来判断这是一个 `IP` 包还是一个 `ARP` 包。这一部分已经在 `main.c` 中实现，不需要我们自己完成。如果判断为 `IP` 包，调用 `handle_ip_packet` 函数，如果判断为 `ARP` 包，则调用 `handle_arp_packet` ，否则报错。

先看 `ip.c` 中的 `handle_ip_packet` 函数。首先我们解析包的目的 `IP` 地址。如果目的 `IP` 为本端口的 `IP` ，那我们需要进一步解析包的 `ICMP` 首部的类型。如果是 `ping` 本端口，那么调用 `icmp_send_packet` 对源主机进行响应，否则把包丢弃。如果目的 `IP` 不是本端口 `IP` ，则说明需要转发这个包。具体来说，先用最长前缀匹配查找路由表，如果查找成功且生存期没有耗尽，则需要重新计算校验和并转发，否则调用 `icmp_send_packet` 向源主机报错。代码实现如下：

```c
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
```

然后我们来看 `icmp_send_packet` 函数的实现。在我设计的代码中，这个函数可以实现的功能可以分为三种，由 `type` 的不同值来区分。当 `type=ICMP_ECHOREPLY` 时，说明是在回复 `ping` 时调用了此函数；当 `type=ICMP_TIME_EXCEEDED` 时，说明生存期耗尽；当 `type=ICMP_DEST_UNREACH` 时，说明查找路由表时失败，目的不可达。根据不同的类型，我们要配置发送的数据包，包括数据包的大小和其中存放的数据，然后调用 `ip_send_packet` 将这个包发出去。代码实现如下：

```c
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
```

在发送数据包时调用了 `ip_send_packet` 函数。这个函数的功能非常简单，在 `ip_base.c` 中实现。

```c
// send IP packet
//
// Different from forwarding packet, ip_send_packet sends packet generated by
// router itself. This function is used to send ICMP packets.
void ip_send_packet(char *packet, int len)
{
	// fprintf(stderr, "lab09 added: send ip packet.\n");
	
	struct iphdr *pkt_IPhead = packet_to_ip_hdr(packet);
	u32 dst_ip = ntohl(pkt_IPhead->daddr);
	rt_entry_t *rt = longest_prefix_match(dst_ip);
	
	if (rt->gw) // 该路由器任何端口IP都与目的IP不在同一网段
		iface_send_packet_by_arp(rt->iface, rt->gw, packet, len);
	else // 在同一网段
		iface_send_packet_by_arp(rt->iface, dst_ip, packet, len);
}
```

其中调用的 `iface_send_packet_by_arp` 函数在 `arp.c` 中实现，函数 `arpcache_lookup` 和 `arpcache_append_packet` 在 `arpcache.c` 中实现。我们分析一下后面两个操作 `ARP cache` 的函数。

`arpcache_lookup` 的功能是查询 `ARP cache`。如果查找到了返回1，否则返回0。具体代码为：

```c
// lookup the IP->mac mapping
//
// traverse the table to find whether there is an entry with the same IP
// and mac address with the given arguments
int arpcache_lookup(u32 ip4, u8 mac[ETH_ALEN])
{
	// fprintf(stderr, "lab09 added: lookup ip address in arp cache.\n");
	
	pthread_mutex_lock(&arpcache.lock);
	int i=0;
	for (i=0; i<MAX_ARP_SIZE; i++) {
		if (arpcache.entries[i].ip4 && arpcache.entries[i].valid == ip4) {
			memcpy(mac,arpcache.entries[i].mac,ETH_ALEN);
			pthread_mutex_unlock(&arpcache.lock);
			return 1;
		}
	}
	pthread_mutex_unlock(&arpcache.lock);
	
	return 0;
}
```

当查询 `ARP cache` 失败时调用 `arpcache_append_packet` 函数，将要发送的包挂起，并且发送 `ARP` 请求。具体代码为：

```c
// append the packet to arpcache
//
// Lookup in the list which stores pending packets, if there is already an
// entry with the same IP address and iface (which means the corresponding arp
// request has been sent out), just append this packet at the tail of that entry
// (the entry may contain more than one packet); otherwise, malloc a new entry
// with the given IP address and iface, append the packet, and send arp request.
void arpcache_append_packet(iface_info_t *iface, u32 ip4, char *packet, int len)
{
	// fprintf(stderr, "lab09 added: append the ip address if lookup failed, and send arp request if necessary.\n");
	struct arp_req *wait_list = NULL;
	struct cached_pkt *cached_packet = NULL;
	cached_packet = (struct cached_pkt*)malloc(sizeof(struct cached_pkt)); // 用来缓存packet和len
	cached_packet->packet = packet;
	cached_packet->len = len;
	
	int found=0;
	
	pthread_mutex_lock(&arpcache.lock);
	
	list_for_each_entry(wait_list, &(arpcache.req_list), list){
		if (wait_list->ip4 == ip4 && wait_list->iface == iface){
			found = 1;
			break;
		}
	}
	
	if (found)
		list_add_tail(&(cached_packet->list), &(wait_list->cached_packets)); // 将cached_packet添加到该结点的末端
	else{
		struct arp_req *req = (struct arp_req*)malloc(sizeof(struct arp_req)); // 新建一个arp_req结点，用来保存cached_packet
		req->iface = iface;
		req->ip4 = ip4;
		req->sent = time(NULL);
		req->retries = 1;
		
		init_list_head(&(req->cached_packets));
		list_add_tail(&(cached_packet->list), &(req->cached_packets));
		list_add_tail(&(req->list), &(arpcache.req_list));
		arp_send_request(iface, ip4);
	}
	
	pthread_mutex_unlock(&arpcache.lock);
}
```

然后我们再回到 `ip.c` ，来分析一下 `handle_arp_packet` 函数的功能。首先我们判断目标地址是否为本端口，如果不是，丢弃这个包，否则，分析报文类型是 `Reply` 还是 `Request` ，在进行相应的操作。

```c
void handle_arp_packet(iface_info_t *iface, char *packet, int len)
{
	// fprintf(stderr, "lab09 added: process arp packet: arp request & arp reply.\n");
	
	struct ether_arp * ether_arp_pkt = (struct ether_arp *)(packet + ETHER_HDR_SIZE);
	
	if (ntohl(ether_arp_pkt->arp_tpa) == iface->ip){ // 判断目的地址是否为本端口
		
		if (ntohs(ether_arp_pkt->arp_op) == ARPOP_REPLY) // Reply报文
			arpcache_insert(ntohl(ether_arp_pkt->arp_spa), ether_arp_pkt->arp_sha); // 自己的请求报文得到回应，要保存到ARP cache中
		else if (ntohs(ether_arp_pkt->arp_op) == ARPOP_REQUEST) // Request报文，请求获得本端口的mac地址
			arp_send_reply(iface, ether_arp_pkt); // 回复自己的信息给源主机
	}
}
```

其中的两个关键函数是 `arpcache_insert` 和 `arp_send_reply` ，我们来看一下这两个函数的功能是怎么实现的。先看 `arpcache_insert`，这个函数在 `arpcache.c` 中实现，它的主要功能是：收到 `ARP` 应答后，将对应的 `IP` 到 `mac` 的映射写入缓存中，并且查看是否有因为等待此映射而暂时挂起的包，把这样的数据包发射出去。因为代码较长，所以代码功能可以参考代码段中的注释。

```c
// insert the IP->mac mapping into arpcache, if there are pending packets
// waiting for this mapping, fill the ethernet header for each of them, and send
// them out
void arpcache_insert(u32 ip4, u8 mac[ETH_ALEN])
{
	// fprintf(stderr, "lab09 added: insert ip->mac entry, and send all the pending packets.\n");
	
	struct arp_req *wait_list = NULL;
	struct arp_req *wait_list_next = NULL;
	
	int found=0;
	int i=0;
	
	pthread_mutex_lock(&arpcache.lock);
	
	for (i=0; i<MAX_ARP_SIZE; i++){ // 遍历arpcache
		if (!arpcache.entries[i].valid){ // valid==0说明表项无效，这里是想要找到一个有效的表项
			found = 1;
			arpcache.entries[i].ip4 = ip4;
			memcpy(arpcache.entries[i].mac, mac, ETH_ALEN);
			arpcache.entries[i].added = time(NULL);
			arpcache.entries[i].valid = 1;
			break;
		}
	}
	
	if (!found){ // 没有找到
		time_t now = time(NULL);
		int index = (u16)now % 32; // 随机生成的0～31的整数值
        	// 对这一项进行覆盖
		arpcache.entries[index].ip4 = ip4;
		memcpy(arpcache.entries[index].mac, mac, ETH_ALEN);
		arpcache.entries[index].added = now;
		arpcache.entries[index].valid = 1;
	}
	
	list_for_each_entry_safe(wait_list, wait_list_next, &(arpcache.req_list), list) {
		if (wait_list->ip4 == ip4){ // 挂起的队列中有结点的IP与ip4相等
			struct cached_pkt *tmp = NULL;
			struct cached_pkt *tmp_next;
			
			list_for_each_entry_safe(tmp, tmp_next, &(wait_list->cached_packets), list){
				memcpy(tmp->packet, mac, ETH_ALEN); // 往首部中填充mac
				iface_send_packet(wait_list->iface, tmp->packet, tmp->len); // 通过记录的端口发送
				free(tmp); // 释放空间
			}
			
			list_delete_entry(&(wait_list->list));
			free(wait_list);
		}
	}
	pthread_mutex_unlock(&arpcache.lock);
}
```

再来看 `arp_send_reply`。这个函数的功能是，构建一个包，对这个包进行预处理，等装填好后将这个包发送出去，即为 `ARP` 应答。

```c
// send an arp reply packet: encapsulate an arp reply packet, send it out
// through iface_send_packet
void arp_send_reply(iface_info_t *iface, struct ether_arp *req_hdr)
{
	// fprintf(stderr, "lab09 added: send arp reply when receiving arp request.\n");
	
	char * packet = (char *) malloc(ETHER_HDR_SIZE + sizeof(struct ether_arp));
	
	struct ether_header * ether_hdr = (struct ether_header *)packet;
	struct ether_arp * ether_arp_pkt = (struct ether_arp *)(packet + ETHER_HDR_SIZE);
	
	ether_hdr->ether_type = htons(ETH_P_ARP); // ether_type：ARP
	memcpy(ether_hdr->ether_shost, iface->mac, ETH_ALEN); // ether_shost：端口的 mac 地址
	memcpy(ether_hdr->ether_dhost, req_hdr->arp_sha, ETH_ALEN); // ether_dhost：收到的ARP报文中的arp_sender hardware address
	
	ether_arp_pkt->arp_hrd = htons(ARPHRD_ETHER); // arp_header：ARPHRD_ETHER（1）
	ether_arp_pkt->arp_pro = htons(ETH_P_IP); // arp_protocol：ETH_P_IP（0x0800）
	ether_arp_pkt->arp_hln = (u8)ETH_ALEN; // 硬件地址长度：ETH_ALEN（6）
	ether_arp_pkt->arp_pln = (u8)4; // 协议地址的长度：4
	ether_arp_pkt->arp_op = htons(ARPOP_REPLY); // opcode：REPLY
	memcpy(ether_arp_pkt->arp_sha, iface->mac, ETH_ALEN); // 发送方硬件地址：当前端口的mac地址
	ether_arp_pkt->arp_spa = htonl(iface->ip); // 发送方协议地址：当前端口的IP地址
	memcpy(ether_arp_pkt->arp_tha, req_hdr->arp_sha, ETH_ALEN); // 目标硬件地址：收到的ARP报文中的 arp_sender hardware address
	ether_arp_pkt->arp_tpa = req_hdr->arp_spa; // 目标协议地址：收到的ARP报文中的arp_sender protocol address
	
	iface_send_packet(iface, packet, ETHER_HDR_SIZE + sizeof(struct ether_arp));
}
```



### 3. 实验结果与分析

（1）运行网络拓扑 `router_topo.py`

`h1#` ping 10.0.1.1(r1) succees

`h1#` ping 10.0.2.22(h2) success

`h1#` ping 10.0.3.33(h3) success

<img src="/Users/smx1228/Desktop/截屏2021-05-13 上午11.27.18.png" alt="截屏2021-05-13 上午11.27.18" style="zoom:30%;" />

`h1#` ping 10.0.3.11 Destination Host Unreachable

`h1#` ping 10.0.4.1 Destination Net Unreachable

<img src="/Users/smx1228/Desktop/截屏2021-05-13 上午11.27.53.png" alt="截屏2021-05-13 上午11.27.53" style="zoom:30%;" />

（2）运行网络拓扑 `my_topo.py`

`h1#` ping 10.0.2.22(h2) success

`h2#` ping 10.0.1.11(h1) success

<img src="/Users/smx1228/Desktop/截屏2021-05-13 上午11.33.53.png" alt="截屏2021-05-13 上午11.33.53" style="zoom:25%;" />

`h1#` traceroute 10.0.2.22 返回路径上每个结点，success

<img src="/Users/smx1228/Desktop/截屏2021-05-13 上午11.36.31.png" alt="截屏2021-05-13 上午11.36.31" style="zoom:25%;" />



## 实验总结与思考题

---

本次实验的代码量很大，因此在开始写之前要理清函数之间的关系，可以用思维导图等方式来进行梳理，这样可以提高书写和修改的效率。



## 参考资料：

---

1. ARP包详解：https://blog.csdn.net/kl1125290220/article/details/45459571
2. ICMP协议：https://www.cnblogs.com/jingmoxukong/p/3811262.html
3. Linux下traceroute的安装和使用：https://blog.csdn.net/huangzx3/article/details/82935697

