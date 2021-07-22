#include "tcp.h"
#include "tcp_sock.h"
#include "tcp_timer.h"

#include "log.h"
#include "ring_buffer.h"

#include <stdlib.h>
#include <stdio.h>

// update the snd_wnd of tcp_sock
//
// if the snd_wnd before updating is zero, notify tcp_sock_send (wait_send)
static inline void tcp_update_window(struct tcp_sock *tsk, struct tcp_cb *cb)
{
	u16 old_snd_wnd = tsk->snd_wnd;
	tsk->snd_wnd = cb->rwnd;
	if (old_snd_wnd == 0)
		wake_up(tsk->wait_send);
}

// update the snd_wnd safely: cb->ack should be between snd_una and snd_nxt
static inline void tcp_update_window_safe(struct tcp_sock *tsk, struct tcp_cb *cb)
{
	if (less_or_equal_32b(tsk->snd_una, cb->ack) && less_or_equal_32b(cb->ack, tsk->snd_nxt))
		tcp_update_window(tsk, cb);
}

#ifndef max
#	define max(x,y) ((x)>(y) ? (x) : (y))
#endif

// check whether the sequence number of the incoming packet is in the receiving
// window
static inline int is_tcp_seq_valid(struct tcp_sock *tsk, struct tcp_cb *cb)
{
	u32 rcv_end = tsk->rcv_nxt + max(tsk->rcv_wnd, 1);
	if (less_than_32b(cb->seq, rcv_end) && less_or_equal_32b(tsk->rcv_nxt, cb->seq_end)) {
		return 1;
	}
	else {
		log(ERROR, "received packet with invalid seq, drop it.");
		return 0;
	}
}

void tcp_recv_data(struct tcp_sock *tsk, struct tcp_cb *cb, char *packet)
{
	if(less_than_32b(cb->seq, tsk->rcv_nxt))
		return;
	
	ofo_packet_enqueue(tsk, cb, packet); 
	ofo_packet_dequeue(tsk); 
	tsk->snd_una = (greater_than_32b(cb->ack, tsk->snd_una))?cb->ack :tsk->snd_una;
	tcp_set_retrans_timer(tsk);
	tcp_send_data(tsk, "data_recv!",sizeof("data_recv!"));
}

// Process the incoming packet according to TCP state machine. 
void tcp_process(struct tcp_sock *tsk, struct tcp_cb *cb, char *packet)
{
	//fprintf(stdout, "lab13 added: implement %s please.\n", __FUNCTION__);
	
	// printf("flags: 0x%x\n",cb->flags);
	// printf("state: %s\n",tcp_state_to_str(tsk->state));
	// printf("rcv_nxt:%u, ack:%u, seq:%u, len:%d\n", tsk->rcv_nxt, cb->ack, cb->seq, cb->pl_len);
	
	if(!tsk)
		return ;
	
	if(cb->flags & TCP_RST){
		tcp_sock_close(tsk);
		free(tsk);
		return ;
	}
	
	if(tsk->state == TCP_CLOSED){
		tcp_send_reset(cb);
		return;
	}
	
	if(tsk->state == TCP_LISTEN){
		if(tsk==NULL){
			tcp_send_reset(cb);
			return ;
		}
		else if((cb->flags & TCP_SYN) == 0){
			if(cb->flags & TCP_RST)
				return ;
			else{
				tcp_send_reset(cb);
				return ;
			}
		}
		printf("***** Start New TCP Connection *****\n");
		struct tcp_sock *child_sock = alloc_tcp_sock();
		child_sock->sk_sip = cb->daddr;
		child_sock->sk_sport = cb->dport;
		child_sock->sk_dip = cb->saddr;
		child_sock->sk_dport = cb->sport;
		child_sock->iss = tcp_new_iss();
		child_sock->snd_nxt = child_sock->iss;
		child_sock->rcv_nxt = cb->seq + 1;
		child_sock->parent = tsk;
		
		list_add_tail(&child_sock->list, &tsk->listen_queue); // add child sock to the listen_queue of parent sock
		tcp_set_state(child_sock, TCP_SYN_RECV);
		tcp_set_retrans_timer(child_sock);
		tcp_send_control_packet(child_sock, TCP_SYN|TCP_ACK);
		tcp_hash(child_sock);
		
		return ;
	}
	
	if(tsk->state == TCP_SYN_SENT){
		if( (cb->flags & (TCP_SYN|TCP_ACK)) == (TCP_SYN|TCP_ACK) ){
			tsk->rcv_nxt = cb->seq + 1;
			tsk->snd_una = cb->ack;
			
			send_buffer_ACK(tsk, cb->ack);
			tcp_unset_retrans_timer(tsk);
			tcp_set_state(tsk, TCP_ESTABLISHED);
			tcp_send_control_packet(tsk, TCP_ACK);
			wake_up(tsk->wait_connect);
		}
		return ;
	}
	
	
	if(tsk->state == TCP_SYN_RECV){
		if(cb->flags & TCP_ACK){
			struct tcp_sock *csk = tsk;
			struct tcp_sock *parent_tsk = csk->parent;
			
			tcp_sock_accept_enqueue(csk);
			csk->rcv_nxt = cb->seq;
			tsk->snd_una = cb->ack;
			send_buffer_ACK(tsk, cb->ack);
			tcp_unset_retrans_timer(tsk);
			
			tcp_set_state(csk, TCP_ESTABLISHED);
			wake_up(parent_tsk->wait_accept);
		}
		return ;
	}
	
	
	if(tsk->state == TCP_ESTABLISHED){
		if (cb->flags & TCP_FIN){
			tsk->rcv_nxt = cb->seq+1;
			wait_exit(tsk->wait_recv);
			wait_exit(tsk->wait_send);
			tcp_set_state(tsk, TCP_CLOSE_WAIT);
			tcp_send_control_packet(tsk, TCP_ACK);
			return ;
		}
		else if (cb->flags & TCP_ACK){
			if (cb->pl_len == 0 || strcmp(cb->payload,"data_recv!") == 0){ // ACK
				tsk->snd_una = cb->ack;
				tsk->rcv_nxt = cb->seq +1;
				tcp_update_window_safe(tsk, cb);
				send_buffer_ACK(tsk, cb->ack);
				tcp_update_retrans_timer(tsk);
				wake_up(tsk->wait_send);
				return;
			}
			else{ // data
				tcp_recv_data(tsk, cb, packet);
				return ;
			}
		}
	}
	
	if (tsk->state == TCP_LAST_ACK){
		if(cb->flags & TCP_ACK){
			send_buffer_ACK(tsk, cb->ack);
			tcp_unset_retrans_timer(tsk);
			tcp_set_state(tsk, TCP_CLOSED);
			tcp_unhash(tsk);
			printf("***** TCP Connection Down *****\n");
			return;
		}
	}
	
	if (tsk->state == TCP_FIN_WAIT_1){
		if(cb->flags & TCP_FIN){
			tcp_set_state(tsk, TCP_CLOSING);
			tcp_send_control_packet(tsk, TCP_ACK);
			return;
		}
		else{
			send_buffer_ACK(tsk, cb->ack);
			tcp_unset_retrans_timer(tsk);
			tcp_set_state(tsk, TCP_FIN_WAIT_2);
			return;
		}
	}
	
	if (tsk->state == TCP_FIN_WAIT_2){
		if(cb->flags & TCP_FIN){
			tsk->rcv_nxt = cb->seq+1;
			tcp_set_state(tsk, TCP_TIME_WAIT);
			tcp_set_timewait_timer(tsk);
			tcp_send_control_packet(tsk, TCP_ACK);
			tcp_unhash(tsk);
			return;
		}
	}
	
	if (tsk->state == TCP_CLOSING){
		if(cb->flags & TCP_ACK){
			send_buffer_ACK(tsk, cb->ack);
			tcp_unset_retrans_timer(tsk);
			tcp_set_state(tsk, TCP_TIME_WAIT);
			tcp_set_timewait_timer(tsk);
			tcp_unhash(tsk);
			return;
		}
	}
	
	tcp_send_control_packet(tsk, TCP_ACK);
	return;
}
