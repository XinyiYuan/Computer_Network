main.c run_application 创建进程tcp_server和tcp_client

main.c ustack_run 调用handle_packet处理收到的数据包

main.c handle_packet 分析是ip包（调用handle_ip_packet）还是arp包（调用handle_arp_packet）

ip.c handle_ip_packet 分析是icmp包 还是tcp包（调用handle_tcp_packet(packet, ip, (struct tcphdr *)(IP_DATA(ip)))）

tcp.c handle_tcp_packet（==接收数据包的处理流程==） 检查校验和，init_cb，查找tsk，调用tcp_process(tsk, &cb, packet);

tcp_cb, tcp_sock（tcp_sock.h）：

```c
// control block, representing all the necesary information of a packet
struct tcp_cb {
	u32 saddr;		// source addr of the packet
	u32 daddr;		// source port of the packet
	u16 sport;		// dest addr of the packet
	u16 dport;		// dest port of the packet
	u32 seq;		// sequence number in tcp header
	u32 seq_end;		// seq + (SYN|FIN) + len(payload)
	u32 ack;		// ack number in tcp header
	u32 rwnd;		// receiving window in tcp header
	u8 flags;		// flags in tcp header
	struct iphdr *ip;		// pointer to ip header
	struct tcphdr *tcp;		// pointer to tcp header
	char *payload;		// pointer to tcp data
	int pl_len;		// the length of tcp data
};

// the main structure that manages a connection locally
struct tcp_sock {
	// sk_ip, sk_sport, sk_sip, sk_dport are the 4-tuple that represents a 
	// connection
	struct sock_addr local;
	struct sock_addr peer;
#define sk_sip local.ip
#define sk_sport local.port
#define sk_dip peer.ip
#define sk_dport peer.port
    
sock_addr包含ip和port信息
	// pointer to parent tcp sock, a tcp sock which bind and listen to a port 
	// is the parent of tcp socks when *accept* a connection request
	struct tcp_sock *parent;

	// represents the number that the tcp sock is referred, if this number 
	// decreased to zero, the tcp sock should be released
	int ref_cnt;

	// hash_list is used to hash tcp sock into listen_table or established_table, 
	// bind_hash_list is used to hash into bind_table
	struct list_head hash_list;
	struct list_head bind_hash_list;

	// when a passively opened tcp sock receives a SYN packet, it mallocs a child 
	// tcp sock to serve the incoming connection, which is pending in the 
	// listen_queue of parent tcp sock
	struct list_head listen_queue;
    当被动建立连接的parent socket收到SYN数据包后，会产生一个child socket来服务该连接，放到parent socket的listen_queue队列中
        
	// when receiving the last packet (ACK) of the 3-way handshake, the tcp sock 
	// in listen_queue will be moved into accept_queue, waiting for *accept* by 
	// parent tcp sock
	struct list_head accept_queue;
    当接收到三次握手中的最后一个包（ACK）时，在listen_queue中的child socket会放到accept_queue中，等待应用程序读取(tcp_sock_accept)
Socket加入到accept_queue中时，parent socket的accept_backlog值加一，离开队列时该值减一，注意accept_backlog < backlog


#define TCP_MAX_BACKLOG 128
	// the number of pending tcp sock in accept_queue
	int accept_backlog;
	// the maximum number of pending tcp sock in accept_queue
	int backlog;

	// the list node used to link listen_queue or accept_queue of parent tcp sock
	struct list_head list; 用于将该socket放入到parent socket的队列中
	// tcp timer used during TCP_TIME_WAIT state
	struct tcp_timer timewait;

	// used for timeout retransmission
	struct tcp_timer retrans_timer;

	// synch waiting structure of *connect*, *accept*, *recv*, and *send*
	struct synch_wait *wait_connect;
	struct synch_wait *wait_accept;
	struct synch_wait *wait_recv;
	struct synch_wait *wait_send;

	// receiving buffer
	struct ring_buffer *rcv_buf;
	// used to pend unacked packets
	struct list_head send_buf;
	// used to pend out-of-order packets
	struct list_head rcv_ofo_buf;

	// tcp state, see enum tcp_state in tcp.h
	int state;

	// initial sending sequence number 本端初始发送序列号
	u32 iss;

	// the highest byte that is ACKed by peer 对端连续确认的最大序列号
	u32 snd_una;
	// the highest byte sent 本端已发送的最大序列号
	u32 snd_nxt;

	// the highest byte ACKed by itself (i.e. the byte expected to receive next) 本端连续接收的最大序列号
	u32 rcv_nxt;

	// used to indicate the end of fast recovery
	u32 recovery_point;		

	// min(adv_wnd, cwnd)
	u32 snd_wnd;
	// the receiving window advertised by peer
	u16 adv_wnd;

	// the size of receiving window (advertised by tcp sock itself)
	u16 rcv_wnd;

	// congestion window
	u32 cwnd;

	// slow start threshold
	u32 ssthresh;
};
```

tcp_hash_table（tcp_hash.h）：

```c
// the 3 tables in tcp_hash_table
struct tcp_hash_table {
	struct list_head established_table[TCP_HASH_SIZE]; // 源目的地址、源目的端口都已经确定下来的socket
	struct list_head listen_table[TCP_HASH_SIZE]; // 只知道源地址、源端口的socket
	struct list_head bind_table[TCP_HASH_SIZE]; // 任何占用一个本地端口的socket
};
```

对于一个新到达的数据包，先在established_table中查找相应socket，如果没有找到，再到listen_table中查找相应socket

handle_tcp_packet（tcp.c）：

```c
// let tcp control block (cb) to store all the necessary information of a TCP
// packet
void tcp_cb_init(struct iphdr *ip, struct tcphdr *tcp, struct tcp_cb *cb)
{
	int len = ntohs(ip->tot_len) - IP_HDR_SIZE(ip) - TCP_HDR_SIZE(tcp);
	cb->saddr = ntohl(ip->saddr);
	cb->daddr = ntohl(ip->daddr);
	cb->sport = ntohs(tcp->sport);
	cb->dport = ntohs(tcp->dport);
	cb->seq = ntohl(tcp->seq);
	cb->seq_end = cb->seq + len + ((tcp->flags & (TCP_SYN|TCP_FIN)) ? 1 : 0);
	cb->ack = ntohl(tcp->ack);
	cb->payload = (char *)tcp + tcp->off * 4;
	cb->pl_len = len;
	cb->rwnd = ntohs(tcp->rwnd);
	cb->flags = tcp->flags;
}

// handle TCP packet: find the appropriate tcp sock, and let the tcp sock 
// to process the packet.
void handle_tcp_packet(char *packet, struct iphdr *ip, struct tcphdr *tcp)
{
	if (tcp_checksum(ip, tcp) != tcp->checksum) {
		log(ERROR, "received tcp packet with invalid checksum, drop it.");
		return ;
	}

	struct tcp_cb cb;
	tcp_cb_init(ip, tcp, &cb); // 在cb中存储tcp头部所有信息

	struct tcp_sock *tsk = tcp_sock_lookup(&cb); // 先查找established表 tcp_sock_lookup_established，在查找listen表 tcp_sock_lookup_listenfref_cntf

	tcp_process(tsk, &cb, packet);
}

```

tcp_in.c tcp_process 状态机