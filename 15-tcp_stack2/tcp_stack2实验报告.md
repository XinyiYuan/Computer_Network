# <font size = '20'> <b> 网络传输机制实验二 </b> </font>


<center> 中国科学院大学 </center>

<center>袁欣怡 2018K8009929021</center>

<center> 2021.6.23 </center>

[TOC]

## 实验内容

---

在之前的实验中，我们已经实现`TCP`的连接和断开功能。本次实验需要在之前的基础上实现数据传输，使得结点之间能在无丢包网络环境中传输数据。为了完成测试中字符串和文件的`echo`，我们需要实现的内容包括：

1. 利用环形缓存`Receiving Buffer`完成数据的接收和缓存；
2. 根据`recv_window`，完成基础的流量控制；
3. 封装数据包并发送。



## 实验流程

---

### 1. 搭建实验环境

本实验中涉及到的文件主要有：

- `main.c`：编译后生成`tcp_stack`可执行文件，在两个`host`结点上运行。

- `Makefile`：处理终端`make all`和`make clean`命令。

- `tcp_apps.c`：完成服务器功能的函数`tcp_server`和完成用户端功能的函数`tcp_client`。本次实验1和实验2需要使用不同的`tcp_apps.c`，其中一个实现字符串的`echo`，一个实现数据文件的`echo`。

- `tcp_in.c`：接收报文并处理需要的函数。本次实验修改其中`tcp_process`函数。

- `tcp_sock.c`：`socket`相关操作需要的函数。本次实验实现其中`tcp_sock_read`和`tcp_sock_write`函数。

- `tcp_topo.py`：实现如下图的网络拓扑结构。

  <img src="/Users/smx1228/Desktop/截屏2021-06-23 下午11.49.18.png" alt="截屏2021-06-23 下午11.49.18" style="zoom:30%;" />

- `tcp_stack.py`：`python`语言实现的服务器程序和用户端程序，方便调试。

### 2. 实验代码设计

#### 字符串echo

1. 状态响应函数

   `server`和`client`建立连接之后，双方均处于`ESTABLISHED`状态。之前实验中，此时会收到`FIN`包，断开连接。本次实验中需要添加收到非`FIN`的数据包的处理。非`FIN`包有两种可能，`ACK`报文（数据长度=0）和数据报文（数据长度>0）。具体处理见如下代码（`exp1_code/tcp_in.c`）：

   ```c
   // tcp_process函数中部分：
   if (tsk->state == TCP_ESTABLISHED) {
   	if (tcp->flags & TCP_FIN) {
   		tcp_set_state(tsk, TCP_CLOSE_WAIT);
   		tsk->rcv_nxt = cb->seq + 1;
   		tcp_send_control_packet(tsk, TCP_ACK);
   	}
   	else if (tcp->flags & TCP_ACK) {
   		if (cb->pl_len == 0) { // ACK报文
               	// 说明自己是消息发送方，此时需要修改snd_una和rcv_nxt
               	// 还要更新发送窗口大小
   			tsk->snd_una = cb->ack;
   			tsk->rcv_nxt = cb->seq + 1;
   			tcp_update_window_safe(tsk, cb);
   			return ;
   		} else { // 数据报文
   			handle_recv_data(tsk, cb);
   			return ;
   		}
   	}
   }
   ```

   其中调用的`handle_recv_data`函数也在`exp1_code/tcp_in.c`中实现：

   ```c
   void handle_recv_data(struct tcp_sock *tsk, struct tcp_cb *cb) {]
   	for(int i=0; i<cb->pl_len; ){
   		while(ring_buffer_full(tsk->rcv_buf)){ // 检查rcv_buf是否已满
   			if(sleep_on(tsk->wait_recv)<0){
   				return ;
   			}
   		}
   		
   		int wsize =  min(ring_buffer_free(tsk->rcv_buf), cb->pl_len-i);
   		write_ring_buffer(tsk->rcv_buf, cb->payload+i, wsize); // 填入buf
   		i += wsize;
   		wake_up(tsk->wait_recv);
   		
   	}
   	tsk->rcv_nxt = cb->seq + cb->pl_len;
   	tsk->snd_una = cb->ack;
   	tcp_send_control_packet(tsk, TCP_ACK);
   }
   ```

2. 读写函数

   在`tcp_apps.c`中还要调用`tcp_sock_read`和`tcp_sock_write`函数，前者从环形`buf`中读取数据到应用，后者发送数据。具体实现代码如下（`exp1_code/tcp_sock.c`）：

   ```c
   //lab15 added:tcp_sock_read and tcp_sock_write
   int tcp_sock_read(struct tcp_sock *tsk, char *buf, int len) {
   	while (ring_buffer_empty(tsk->rcv_buf)) { // 检查buf非空
   		sleep_on(tsk->wait_recv);
   	}
   
   	int rlen = read_ring_buffer(tsk->rcv_buf, buf, len); // 读取数据
   	wake_up(tsk->wait_recv);
   	return rlen;
   }
   
   void send_data(struct tcp_sock *tsk, char *buf, int len) {
   	int send_packet_len = ETHER_HDR_SIZE + IP_BASE_HDR_SIZE + TCP_BASE_HDR_SIZE + len;
   	char * packet = (char *)malloc(send_packet_len);
   	memcpy(packet + ETHER_HDR_SIZE + IP_BASE_HDR_SIZE + TCP_BASE_HDR_SIZE, buf, len);
   	tcp_send_packet(tsk, packet, send_packet_len);
   }
   
   int tcp_sock_write(struct tcp_sock *tsk, char *buf, int len) {
   	int single_len = 0;
   	int init_seq = tsk->snd_una;
   	int init_len = len;
   
   	while (len > 1514 - ETHER_HDR_SIZE - IP_BASE_HDR_SIZE - TCP_BASE_HDR_SIZE) {
   		single_len = min(len, 1514 - ETHER_HDR_SIZE - IP_BASE_HDR_SIZE - TCP_BASE_HDR_SIZE); // 每次读取1个数据包大小的数据
   		send_data(tsk, buf + (tsk->snd_una - init_seq), single_len); // send_data负责调用tcp_send_packet发送数据包
   		if (tsk->snd_wnd == 0) {
   			sleep_on(tsk->wait_send);
   		}
   		len -= single_len;
   	}
   
   	send_data(tsk, buf + (tsk->snd_una - init_seq), len);
   	return init_len;
   }
   ```

#### 文件echo

主要需要修改`tcp_apps.c`中的`tcp_client`函数，再修改`tcp_server`让两者匹配。具体代码如下（`exp2_code/tcp_apps.c`）：

```c
// tcp client application, connects to server (ip:port specified by arg), each
// time sends one bulk of data and receives one bulk of data 
void *tcp_client(void *arg)
{
	struct sock_addr *skaddr = arg;
	struct tcp_sock *tsk = alloc_tcp_sock();

	if (tcp_sock_connect(tsk, skaddr) < 0) {
		log(ERROR, "tcp_sock connect to server ("IP_FMT":%hu)failed.", \
				NET_IP_FMT_STR(skaddr->ip), ntohs(skaddr->port));
		exit(1);
	}
	
	FILE *fd = fopen("client-input.dat", "r");
	char c;
	char *wbuf = (char *)malloc(20000000);
	
	int wlen = 0;
	while((c = fgetc(fd)) != EOF) {
		wbuf[wlen] = c;
		wlen++;
	}
	
	char rbuf[MAX_LEN + 1];
	int rlen = 0;
	int n = wlen / MAX_LEN + 1;
	
	for (int i = 0; i < n; i++) {
		if(wlen >= MAX_LEN){
			if (tcp_sock_write(tsk, wbuf + i * MAX_LEN, MAX_LEN) < 0)
				break;
		}
		
		else if (wlen < MAX_LEN && wlen > 0) {
			if (tcp_sock_write(tsk, wbuf + i * MAX_LEN, wlen) < 0)
				break;
		}
		
		else if(wlen <= 0) {
			tcp_sock_write(tsk, wbuf, 0);
			break;
		}

		wlen = wlen - MAX_LEN;
		printf("remained wlen:%d\n",wlen);
		
		if (wlen <= 0)
			break;
		
		rlen = tcp_sock_read(tsk, rbuf, MAX_LEN);
		if (rlen == 0) {
			log(DEBUG, "tcp_sock_read return 0, finish transmission.");
			break;
		}
		else if (rlen > 0)
			rbuf[rlen] = '\0';
		else {
			log(DEBUG, "tcp_sock_read return negative value, something goes wrong.");
			exit(1);
		}

		usleep(500);
	}
	tcp_sock_close(tsk);

	return NULL;
}
```



### 3. 启动脚本进行测试

#### 实验1

1. 测试`server`：成功

   > sudo python tcp_topo.py
   >
   > mininet> xterm h1 h2
   >
   > `h1#` ./tcp_stack server 10001
   >
   > `h2#` python tcp_stack.py client 10.0.0.1 10001

   实验结果如下：

   <img src="/Users/smx1228/Desktop/success1-2.png" alt="success1-2" style="zoom:15%;" />

2. 测试`client`：成功

   > sudo python tcp_topo.py
   >
   > mininet> xterm h1 h2
   >
   > `h1#` python tcp_stack.py server 10001
   >
   > `h2#` ./tcp_stack client 10.0.0.1 10001

   实验结果如下：

   <img src="/Users/smx1228/Desktop/success1-1.png" alt="success1-1" style="zoom:15%;" />

3. 同时测试`server`和`client`：成功

   > sudo python tcp_topo.py
   >
   > mininet> xterm h1 h2
   >
   > `h1#` ./tcp_stack server 10001
   >
   > `h2#` ./tcp_stack client 10.0.0.1 10001

   实验结果如下：

   <img src="/Users/smx1228/Desktop/success1'.png" alt="success1'" style="zoom:15%;" />

#### 实验2

同时测试`server`和`client`，结果如下：

> sudo python tcp_topo.py
>
> mininet> xterm h1 h2
>
> `h1#` ./tcp_stack server 10001
>
> `h2#` ./tcp_stack client 10.0.0.1 10001

实验结果如下：

<img src="/Users/smx1228/Desktop/success2.png" alt="success2" style="zoom:15%;" />

然后再终端中检查`server-output.dat`和`client-input.dat`内容是否完全相同：

<img src="/Users/smx1228/Desktop/success2-2'.png" alt="success2-2'" style="zoom:20%;" />



综上，可见本次实验成功。

## 实验总结与思考题

---

本次实验总体顺利，但是遇到了一个尚未解决的小问题：在`server`和`client`均进入`ESTABLISHED`状态后，有一定几率会卡住，如下图所示：

<img src="/Users/smx1228/Desktop/failure1.png" alt="failure1" style="zoom:15%;" />

通过完善锁的处理，失败情况发生的概率可以降低。但是仍无法完全解决。