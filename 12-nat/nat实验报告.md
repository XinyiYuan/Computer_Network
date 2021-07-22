# <font size = '20'><b> 网络地址转换实验 </b> </font>


<center> 中国科学院大学 </center>

<center>袁欣怡 2018K8009929021</center>

<center> 2021.6.3 </center>

[TOC]

## 实验内容

---

网络中的`IP`地址分为公网地址和私网地址两类。为了能让私网地址和公网地址通信且不会暴露私网地址的信息，我们需要使用`nat`进行网络地址转换。`nat`设备的主要工作包括：

1. 维护私网地址/端口与公网地址/端口的映射关系；
2. 对数据包内容进行重写（`Translation`），修改`IP`地址/端口等字段。

`nat`的工作场景可以分为`SNAT`和`DNAT`两种。在本次实验中，我们需要实现`nat`在这两种情况下不同的功能。我们的测试包括三个部分：

1. `SNAT`实验：在`n1`上运行`nat`程序，在`h3`上运行`HTTP Server`，在`h1`和`h2`上分别访问`h3`；
2. `DNAT`实验：在`n1`上运行`nat`程序，在`h1`和`h2`上分别运行`HTTP Server`，在`h3`上访问`h1`和`h2`；
3. 构造一个包含两个`nat`的拓扑，测试主机能否穿过两个`nat`通信。



## 实验流程

---

### 1. 搭建实验环境

本实验中涉及到的文件主要有：

- `main.c`：编译后生成`nat`可执行文件，在路由器结点上运行。

- `nat.c`：`nat`相关代码实现，本次实验中需要完成其中`get_packet_direction`，`do_translation`，`nat_timeout`等函数。

- `Makefile`：处理终端`make all`和`make clean`命令。

- `nat_topo.py`：构建如下图的网络拓扑结构，在前两个测试中使用：

  <img src="/Users/smx1228/Desktop/截屏2021-06-03 上午9.21.29.png" alt="截屏2021-06-03 上午9.21.29" style="zoom:20%;" />

- `my_topo.py`：构建如下图的网络拓扑结构，在最后一个测试中使用：

  <img src="/Users/smx1228/Desktop/截屏2021-06-03 上午9.21.38.png" alt="截屏2021-06-03 上午9.21.38" style="zoom:20%;" />

- `http_server.py`：简单`HTTP Server`的实现，降低调试困难。

- `exp*.conf`：`nat`结点的配置文件，内容包括`internal-iface`和`external-iface`，可能还包括`dnat-rules`。`nat.c`中的函数`parse_config`会对其进行解析。


### 2. 实验代码设计

`NAT`：网络地址转换`Network Address Translation`，是一种在`IP`数据包通过路由器或防火墙时重写`IP`源地址或目的地址的技术。`nat`需要维护私网地址/端口与公网地址/端口的映射关系，且在收到数据包时根据数据包的方向和内容判断需要进行何种重写。

重写`Translation`可以分为两类，源地址转换`SNAT`和目的地址转换`DNAT`。当内网主机`h1`向外网主机`h2`发数据时时，如果`h2`想回复`h1`，但是把目的地址设置成内网地址，则数据包无法到达，解决办法是修改`h1`发送的数据包的源地址，这样`h2`回复的数据包才能被路由器转发。这次修改即为`SNAT`。当主机`h1`连接的`nat`路由器收到`h2`回复的数据包后，判断目的地址虽然是公网地址，但在记录中和一私网地址相对应，则修改目的地址以在私网内转发，这个过程是`DNAT`。以下这张图更形象地展示了`nat`工作的过程：

<img src="/Users/smx1228/Desktop/截屏2021-06-03 下午3.56.22.png" alt="截屏2021-06-03 下午3.56.22" style="zoom:18%;" />

实现重写功能的函数为`nat.c`中的`do_translation`，具体代码如下：

```c
// do translation for the packet: replace the ip/port, recalculate ip & tcp
// checksum, update the statistics of the tcp connection
void do_translation(iface_info_t *iface, char *packet, int len, int dir)
{
	// fprintf(stdout, "lab12 added: do translation for this packet.\n");
	
	pthread_mutex_lock(&nat.lock);
	
  	  // 解析报头
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
	
	if (dir == DIR_IN){ // 方向为DIR_IN
		found = 0;
		
		list_for_each_entry(mapping_entry, head, list) { // 查找是否有外网IP/端口号和目的IP/端口号匹配
			if (mapping_entry->external_ip == ntohl(ip->daddr) && mapping_entry->external_port == ntohs(tcp->dport)){
				found = 1;
				break;
			}
		}
		
		if (!found){ // 没有找到，则新建一个表项
			struct nat_mapping *new_entry = (struct nat_mapping*)malloc(sizeof(struct nat_mapping));
			memset(new_entry, 0, sizeof(struct nat_mapping));
			new_entry->external_ip = ntohl(ip->daddr);
			new_entry->external_port = ntohs(tcp->dport);
			
			list_add_tail(&(new_entry->list),head);
			mapping_entry = new_entry;
		}
		
        	// 按照记录的信息修改数据包内容
		tcp->dport = htons(mapping_entry->internal_port);
		ip->daddr = htonl(mapping_entry->internal_ip);
		mapping_entry->conn.external_fin = (tcp->flags == TCP_FIN);
		mapping_entry->conn.external_seq_end = tcp->seq;
		
		if (tcp->flags == TCP_ACK)
			mapping_entry->conn.external_ack = tcp->ack;
	}
	else{ 
		found = 0;
		
		list_for_each_entry(mapping_entry, head, list){ // 查找是否有内网IP/端口号和源IP/端口号匹配
			if (mapping_entry->internal_ip == ntohl(ip->saddr) && mapping_entry->internal_port == ntohs(tcp->sport)){
				found = 1;
				break;
			}
		}
		
		if (!found) { // 没有找到，则新建一个表项
			struct nat_mapping *new_entry = (struct nat_mapping*)malloc(sizeof(struct nat_mapping));
			memset(new_entry, 0, sizeof(struct nat_mapping));
			new_entry->internal_ip = ntohl(ip->saddr);
			new_entry->internal_port = ntohs(tcp->sport);
			new_entry->external_ip = nat.external_iface->ip;
			u16 i;
            
            	      // 遍历可用端口，找到一个i，分配为external_port
			for (i = NAT_PORT_MIN; i < NAT_PORT_MAX; i++)
				if (!nat.assigned_ports[i]){
					nat.assigned_ports[i] = 1;
					break;
				}
		
			new_entry->external_port = i;
			list_add_tail(&(new_entry->list),head);
			mapping_entry = new_entry;
		}
		
        	// 按照记录的信息修改数据包内容
		tcp->sport = htons(mapping_entry->external_port);
		ip->saddr = htonl(mapping_entry->external_ip);
		mapping_entry->conn.internal_fin = (tcp->flags == TCP_FIN);
		mapping_entry->conn.internal_seq_end = tcp->seq;
	
		if (tcp->flags == TCP_ACK)
			mapping_entry->conn.internal_ack = tcp->ack;
	}
	
        // 重新计算tcp校验和和ip校验和
	tcp->checksum = tcp_checksum(ip, tcp);
	ip->checksum = ip_checksum(ip);
	mapping_entry->update_time = time(NULL);
	
	pthread_mutex_unlock(&nat.lock);
	
	ip_send_packet(packet, len);
}
```

其中，判断数据包发送方向的函数为`nat.c`中的`get_packet_direction`：

```c
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
```

其中判断数据包发送方向的函数是`nat.c`中的`get_packet_direction`：

```c
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
```

跟之前的实验一样，本实验中私网地址/端口和公网地址/端口之间的映射关系也需要老化操作。判断可以进行老化操作的标准是：1. 检测到`TCP`连接结束时的四次握手；2. 一方发送了`RST`包；3. 双方超过60秒没有进行过数据传输。在这三种情况下，可以判断本次连接已经结束，存储的响应表项也可以删除。实现老化功能的函数是`nat.c`中的`nat_timeout`：

```c
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
		sleep(1); // 循环每1秒执行一次
	}

	return NULL;
}
```



### 3. 启动脚本进行测试

实验代码可以实现要求的全部功能，实验过程即结果如下：

#### （1）SNAT实验

> sudo python nat_topo.py
>
> mininet> xterm h1 h2 h3 n1
>
> `n1#` ./nat exp1.conf
>
> `h3#` python ./http_server.py
>
> `h1#` wget http://159.226.39.123:8000
>
> `h1#` cat index.html
>
> `h2#` wget http://159.226.39.123:8000
>
> `h2#` cat index.html.1

 实验结果：

<img src="/Users/smx1228/Desktop/截屏2021-06-02 下午4.23.18.png" alt="截屏2021-06-02 下午4.23.18" style="zoom:15%;" />

#### （2）DNAT实验

>sudo python nat_topo.py
>
>mininet> xterm h1 h2 h3 n1
>
>`n1#` ./nat exp1.conf
>
>`h1#` python ./http_server.py
>
>`h2#` python ./http_server.py
>
>`h3#` wget http://159.226.39.43:8000
>
>`h3#` cat index.html.2
>
>`h3#` wget http://159.226.39.43:8001
>
>`h3#` cat index.html.3

实验结果：

<img src="/Users/smx1228/Desktop/截屏2021-06-02 下午4.57.17.png" alt="截屏2021-06-02 下午4.57.17" style="zoom:15%;" />

#### （3）双nat穿透实验

>sudo python my_topo.py
>
>mininet> h1 h2 n1 n2
>
>`n1#` ./nat exp3_1.conf
>
>`n2#` ./nat exp3_2.conf
>
>`h2#` python ./http_server.py
>
>`h1#` wget http://159.226.39.123:8000
>
>`h1#` cat index.html

实验结果：

<img src="/Users/smx1228/Desktop/截屏2021-06-02 下午6.07.24.png" alt="截屏2021-06-02 下午6.07.24" style="zoom:15%;" />



## 实验总结与思考题

---

- 实验中的`NAT`系统可以很容易实现支持`UDP`协议，现实网络中`NAT`还需要对`ICMP`进行地址翻译，请调研说明`NAT`系统如何支持`ICMP`协议。

  假设主机结点`h1`要`ping`结点`h2`，`h1`发送`ICMP`报文，`h1`会根据报文头部中包含的类型信息`Type`和代码信息`Code`生成源端口号，根据报文头部中包含的标识符信息`Identifier`生成目的端口号，即

  | 源IP  | 源端口 | 目的IP | 目的端口 |
  | ----- | ----- | ----- | ----- |
  | h1 IP | Type + Code | h2 IP  | Identifer |

  `nat`路由器收到包后，会添加响应的`nat`表项：

  | 内网IP      | 内网端口    | 协议 | 外网IP     | 外网端口   |
  | ----------- | ----------- | ---- | ---------- | ------- |
  | h1 IP | Type + Code | ICMP | h1 public_IP | IDENTIFIER（随机分配端口号） |

  然后将`ICMP`报头的源端口和目的端口进行相应的修改，修改后结果为：

  | 源IP         | 源端口     | 目的IP | 目的端口  |
  | ------------ | ---------- | ------ | --------- |
  | h1 public_IP | IDENTIFIER | h2 IP  | Identifer |

  结点`h2`收到`ICMP`报文后，可能会生成`ICMP`响应报文，报头内容如下：

  | 源IP  | 源端口      | 目的IP       | 目的端口   |
  | ----- | ----------- | ------------ | ---------- |
  | h2 IP | Type + Code | h1 public_IP | IDENTIFIER |

  `nat`路由器结点收到响应报文后，查找`nat`表项并且确定应该转发给私网中的结点`h1`。两个结点完成一次来回通信。

  

- 给定一个有公网地址的服务器和两个处于不同内网的主机，如何让两个内网主机建立`TCP`连接并进行数据传输。（最好有概念验证代码）

  这种情况是要两个处于不同`nat`网络下的结点建立直接连接，可以称为`nat`穿透。此时每个结点不仅要知道自己的内网地址，还需要知道对方的公网地址/端口，从而发送数据包、建立连接。整个流程中的关键步骤是：
  
  1. 发现自己的公网地址/端口；
  
     常见做法是：发送一个数据包给`server`，`server`通过解析和查表，获得结点的公网地址，然后将地址回传给结点。
  
  2. 将自己的公网地址/端口发送给对方。
  
     常见做法是：通过第三方`server`来交换双方的公网地址/端口。
  
  具体实现代码可以参考：
  
  ```c
  #include <stdio.h>  
  #include <unistd.h>  
  #include <signal.h>  
  #include <sys/socket.h>  
  #include <fcntl.h>  
  #include <stdlib.h>  
  #include <errno.h>  
  #include <string.h>  
  #include <arpa/inet.h>  
    
  #define MAXLINE 128  
  #define SERV_PORT 8877  
    
  typedef struct  
  {  
      char ip[32];  
      int port;  
  }server;  
    
  //发生了致命错误，退出程序  
  void error_quit(const char *str)      
  {      
      fprintf(stderr, "%s", str);   
      //如果设置了错误号，就输入出错原因  
      if( errno != 0 )  
          fprintf(stderr, " : %s", strerror(errno));  
      printf("\n");  
      exit(1);      
  }     
    
  int main(int argc, char **argv)       
  {            
      int i, res, port;  
      int connfd, sockfd, listenfd;   
      unsigned int value = 1;  
      char buffer[MAXLINE];        
      socklen_t clilen;          
      struct sockaddr_in servaddr, sockaddr, connaddr;    
      server other;  
    
      if( argc != 2 )  
          error_quit("Using: ./client <IP Address>");  
    
      //创建用于链接（主服务器）的套接字          
      sockfd = socket(AF_INET, SOCK_STREAM, 0);   
      memset(&sockaddr, 0, sizeof(sockaddr));        
      sockaddr.sin_family = AF_INET;        
      sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);        
      sockaddr.sin_port = htons(SERV_PORT);        
      inet_pton(AF_INET, argv[1], &sockaddr.sin_addr);  
      //设置端口可以被重用  
      setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &value, sizeof(value));  
    
      //连接主服务器  
      res = connect(sockfd, (struct sockaddr *)&sockaddr, sizeof(sockaddr));   
      if( res < 0 )  
          error_quit("connect error");  
    
      //从主服务器中读取出信息  
      res = read(sockfd, buffer, MAXLINE);  
      if( res < 0 )  
          error_quit("read error");  
      printf("Get: %s", buffer);  
    
      //若服务器返回的是first，则证明是第一个客户端  
      if( 'f' == buffer[0] )  
      {  
          //从服务器中读取第二个客户端的IP+port  
          res = read(sockfd, buffer, MAXLINE);  
          sscanf(buffer, "%s %d", other.ip, &other.port);  
          printf("ff: %s %d\n", other.ip, other.port);  
    
          //创建用于的套接字          
          connfd = socket(AF_INET, SOCK_STREAM, 0);   
          memset(&connaddr, 0, sizeof(connaddr));        
          connaddr.sin_family = AF_INET;        
          connaddr.sin_addr.s_addr = htonl(INADDR_ANY);        
          connaddr.sin_port = htons(other.port);      
          inet_pton(AF_INET, other.ip, &connaddr.sin_addr);  
    
          //尝试去连接第二个客户端，前几次可能会失败，因为穿透还没成功，  
          //如果连接10次都失败，就证明穿透失败了（可能是硬件不支持）  
          while( 1 )  
          {  
              static int j = 1;  
              res = connect(connfd, (struct sockaddr *)&connaddr, sizeof(connaddr));   
              if( res == -1 )  
              {  
                  if( j >= 10 )  
                      error_quit("can't connect to the other client\n");  
                  printf("connect error, try again. %d\n", j++);  
                  sleep(1);  
              }  
              else   
                  break;  
          }  
    
          strcpy(buffer, "Hello, world\n");  
          //连接成功后，每隔一秒钟向对方（客户端2）发送一句hello, world  
          while( 1 )  
          {  
              res = write(connfd, buffer, strlen(buffer)+1);  
              if( res <= 0 )  
                  error_quit("write error");  
              printf("send message: %s", buffer);  
              sleep(1);  
          }  
      }  
      //第二个客户端的行为  
      else  
      {  
          //从主服务器返回的信息中取出客户端1的IP+port和自己公网映射后的port  
          sscanf(buffer, "%s %d %d", other.ip, &other.port, &port);  
    
          //创建用于TCP协议的套接字          
          sockfd = socket(AF_INET, SOCK_STREAM, 0);   
          memset(&connaddr, 0, sizeof(connaddr));        
          connaddr.sin_family = AF_INET;        
          connaddr.sin_addr.s_addr = htonl(INADDR_ANY);        
          connaddr.sin_port = htons(other.port);        
          inet_pton(AF_INET, other.ip, &connaddr.sin_addr);  
          //设置端口重用  
          setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &value, sizeof(value));  
    
          //尝试连接客户端1，肯定会失败，但它会在路由器上留下记录，  
          //以帮忙客户端1成功穿透，连接上自己   
          res = connect(sockfd, (struct sockaddr *)&connaddr, sizeof(connaddr));   
          if( res < 0 )  
              printf("connect error\n");  
    
          //创建用于监听的套接字          
          listenfd = socket(AF_INET, SOCK_STREAM, 0);   
          memset(&servaddr, 0, sizeof(servaddr));        
          servaddr.sin_family = AF_INET;        
          servaddr.sin_addr.s_addr = htonl(INADDR_ANY);        
          servaddr.sin_port = htons(port);  
          //设置端口重用  
          setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &value, sizeof(value));  
    
          //把socket和socket地址结构联系起来   
          res = bind(listenfd, (struct sockaddr *)&servaddr, sizeof(servaddr));      
          if( -1 == res )  
              error_quit("bind error");  
    
          //开始监听端口         
          res = listen(listenfd, INADDR_ANY);      
          if( -1 == res )  
              error_quit("listen error");  
    
          while( 1 )  
          {  
              //接收来自客户端1的连接  
              connfd = accept(listenfd,(struct sockaddr *)&sockaddr, &clilen);    
              if( -1 == connfd )  
                  error_quit("accept error");  
    
              while( 1 )  
              {  
                  //循环读取来自于客户端1的信息  
                  res = read(connfd, buffer, MAXLINE);  
                  if( res <= 0 )  
                      error_quit("read error");  
                  printf("recv message: %s", buffer);  
              }  
              close(connfd);  
          }  
      }  
    
      return 0;  
  }  
  ```
  
  
  
## 参考资料

---

  1. [NAT穿透](zhuanlan.zhihu.com/p/86759357)
  
  2. [NAT穿透技术实现代码](https://blog.csdn.net/u013634862/article/details/42968855)

