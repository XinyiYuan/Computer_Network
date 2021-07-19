<center><font size='30'><b> 生成树实验 </b></font></center>


<center> 中国科学院大学 </center>

<center>袁欣怡 2018K8009929021</center>

<center> 2021.4.20 </center>

## 实验内容

1. 补全已有代码，实现生成唯一的、优先级最高的生成树的算法。该算法对于 `four_node_ring.py` 和自己构建的不少于七个结点的拓扑结构，可以输出最小生成树拓扑。
2. 在 `four_node_ring.py` 的基础上添加两个主机结点，运行完生成树程序后这两个主机结点可以 `ping` 通。
3. 调研标准生成树协议，考虑如何应对复杂情况和如何提高效率。

## 实验流程

### 1. 搭建实验环境

本实验中涉及到的文件主要有：

`main.c` ：编译后生成可执行文件 `stp` ，在拓扑结构的四个结点上运行。

`stp.c` ：完成 `stp` 机制相关的函数，本次需要完成其中的 `stp_handle_config_packet` 函数。

`device_internal.c` ：框架内部实现，本次实验中需要在 `init_ustack` 函数中添加 `init_mac_port_table` ，进行 `mac` 初始化。

`broadcast.c` ：实现广播功能，从 `lab04` 中复制过来。

`mac.c` ：实现交换机转发功能，从 `lab05` 中复制过来。

`Makefile` ：处理终端中 `make` 相关指令，本次实验中需要往 `SRCS` 中添加 `broadcast.c` 和 `mac.c` 。

`dump_output.sh` ：方便在终端中快速查看结点的状态信息。

`four_node_ring.py` ：四个结点带环路的网络拓扑结构。

`four_node_add_host.py` ：在 `four_node_ring.py` 中添加两个主机结点后形成的拓扑结构。

`seven_node.py` ：自己构建的七个结点的拓扑结构。

### 2. 实验代码设计

在之前的实验中，我们已经知道，当网络拓扑结构中有环形存在时，会导致广播风暴，极大地降低了数据传播效率。为了解决这个问题，==生成树协议==提出，我们可以在已有的物理链路上构建一个没有环形的虚拟链路。具体来说，我们主动地选择一部分物理链路，用它们构造一个不存在环形且不影响数据传输的逻辑链路。没有被选择的物理链路处于闲置状态，但不会被真正拆除，如果在使用中的链路发生异常，闲置的链路可能会作为替代，以保证网络连通。

在生成树协议中，我们的目标是构造优先级最高的唯一的生成树。这一点体现在三个部分：1. ID最小的结点作为根结点；2. 每个结点选择到根结点优先级最高的路径；3. 优先级顺序：路径开销>所连接结点ID大小>所连接端口ID大小。因此，我们需要在端口和结点分别维护自己的信息，然后通过逐步修改自己的信息，最后形成目标的生成树。

端口需要维护的信息有（ `stp.h` ）：

```c
struct stp_port {
	stp_t *stp;					// pointer to stp

	int port_id;				// port id
	char *port_name;
	iface_info_t *iface;

	int path_cost;				// cost of this port, always be 1 in this lab
    							// 注意：此处是端口所在的网段的开销

	u64 designated_root;		// root switch (the port believes)
	u64 designated_switch;		// the switch sending this config
	int designated_port;		// the port sending this config
	int designated_cost;		// path cost to root on port
    							// 注意：是本端口所在网段到根结点的路径开销
};
```

结点需要维护的信息有（ `stp.h` ）：

```c
struct stp {
	u64 switch_id;

	u64 designated_root;		// switch root (it believes)
	int root_path_cost;			// cost of path to root
	stp_port_t *root_port;		// lowest cost port to root
    						// 初始化此指针为NULL

	long long int last_tick;	// switch timers

	stp_timer_t hello_timer;	// hello timer

	// ports
	int nports;				// 端口数
	stp_port_t ports[STP_MAX_PORTS]; // 结点的所有端口

	pthread_mutex_t lock;
	pthread_t timer_thread;
};
```



由于每个结点没有全局视图，所以结点之间需要通过==配置消息==进行交流。为了方便我们借助 `wireshark` 进行调试，所以我们需要保持自己的配置消息格式可以被 `wireshark` 识别。本次实验中，我们使用 `802.1D STP` 配置消息格式，但是我们只使用其中 `Root Switch ID` 到 `Port ID` 的4个字段。

<img src="/Users/smx1228/Desktop/图片 1.png" alt="图片 1" style="zoom:40%;" />

需要我们本实验中完成的是保证每个结点能独立生成自己的配置消息并发送，配置消息包括的内容有：自己的结点 ID，发送端口 ID， 自己认为的根结点 ID，以及自己到根结点的路径和开销；发送方式：基于二层组播，目的 `mac` 地址为 ==01-80-C2-00-00-00== 。

具体来说，每个结点处理配置消息的函数为 `stp_handle_config_packet` （ `stp.c` ）：

```c
static void stp_handle_config_packet(stp_t *stp, stp_port_t *p,
		struct stp_config *config)
{
	// lab06 added: handle config packet here
	// fprintf(stdout, "TODO: handle config packet here.\n");
	
	int change = 0; // 记录是否根据收到的配置消息修改结点存储的信息
				// change==0表示本端口就是指定端口，change==1表示需要修改
	
	if(p->designated_root > ntohll(config->root_id))
		change = 1;
	else if(p->designated_cost >  ntohl(config->root_path_cost))
		change = 1;
	else if ((p->designated_switch &0xffffffffffff) > (ntohll(config->switch_id) &0xffffffffffff))
		change = 1;
	else if((p->designated_port &0xff)> (ntohs(config->port_id) &0xff))
		change = 1;
	
	if (change == 0) // 本端口就是指定端口
		stp_port_send_config(p); // 发送配置消息即可
	else{
		// 结点更新状态
		p->designated_root   = ntohll(config->root_id);
		p->designated_cost   = ntohl (config->root_path_cost);
		p->designated_switch = ntohll(config->switch_id);
		p->designated_port   = ntohs (config->port_id);
		
		// 查找根端口
		// 先找到任意一个非指定端口
		int root_num = 0;
		int find_root_port = 1;
		int i=0;
		for (i=0; i<stp->nports; i++) {
			if (!stp_port_is_designated(&(stp->ports[i]))) { // 如果不是指定端口
				root_num = i;
				break;
			}
			if (i == stp->nports - 1) // 没有找到非指定端口
				find_root_port = 0;
		}
		
		// 查找优先级最高的非指定端口，这个端口是根端口
		for (i=root_num+1; i<stp->nports; i++) { 
			// i==stp->nports-1（即没有找到非指定端口）时，for循环不会运行
			if (stp_port_is_designated(&(stp->ports[i]))) 
				continue;
			
			int change = 1; // 是否需要修改优先级最高非指定端口，和之前类似
			if(stp->ports[i].designated_root >  stp->ports[root_num].designated_root )
				change = 0;
			else if( stp->ports[i].designated_cost >  stp->ports[root_num].designated_cost )
				change = 0;
			else if ((stp->ports[i].designated_switch &0xffffffffffff) > (stp->ports[root_num].designated_switch &0xffffffffffff))
				// 最高位为1，不能直接比较
				change = 0;
			else if((p->designated_port &0xff)> (stp->ports[root_num].designated_port &0xff))
				change = 0;
			
			if (change) root_num = i;
		}
		
		// 找根端口
		if (!find_root_port) { // 没找到根端口，说明是根结点
			stp->root_port = NULL;
			stp->designated_root = stp->switch_id;
			stp->root_path_cost = 0;
		}
		else { // 选择通过根端口连接到根结点
			stp->root_port = &(stp->ports[root_num]);
			stp->designated_root = stp->root_port->designated_root;
			stp->root_path_cost = stp->root_port->designated_cost + stp->root_port->path_cost;
		}
		
		// 更新所有端口的配置信息
		for (i=0; i<stp->nports; i++) {
			if (stp_port_is_designated(&(stp->ports[i]))) { 
				// 指定端口，更新认为的根结点和路径开销
				stp->ports[i].designated_root = stp->designated_root;
				stp->ports[i].designated_cost = stp->root_path_cost;
			}
			else if((stp->root_path_cost < stp->ports[i].designated_cost)|| (stp->root_path_cost==stp->ports[i].designated_cost)&
				(stp->switch_id < stp->ports[i].designated_switch)  || (stp->root_path_cost==stp->ports[i].designated_cost)&
				(stp->switch_id==stp->ports[i].designated_switch) & (stp->ports[i].port_id==stp->ports[i].designated_port)){
					// 非指定端口，需要考虑是否有可能成为指定端口
					stp->ports[i].designated_switch = stp->switch_id;
					stp->ports[i].designated_port = stp->ports[i].port_id;
					stp->ports[i].designated_root = stp->designated_root;
					stp->ports[i].designated_cost = stp->root_path_cost;
				}
		}
		
		if (!stp_is_root_switch(stp))// 如果结点从根结点变成普通结点
			stp_stop_timer(&(stp->hello_timer));
		
		for (i=0; i<stp->nports; i++)
			if (stp_port_is_designated(&(stp->ports[i]))) // 找到指定端口
				stp_port_send_config(&(stp->ports[i])); // 发送配置消息
	}
}
```

我们还需要补全 `main.c` 中的 `handle_packet` 函数，方便我们测试在运行生成树协议之后拓扑结构中的两个 `host` 结点可以互相 `ping` 通。

handle_packet（main.c）：

```c
void handle_packet(iface_info_t *iface, char *packet, int len)
{
	struct ether_header *eh = (struct ether_header *)packet;
	
	if (memcmp(eh->ether_dhost, eth_stp_addr, sizeof(*eth_stp_addr))) {
        	// eh->ether_dhost != eth_stp_addr，说明这是需要转发的包，而不是生成树中的配置消息
		// lab06 added: forward this packet, if this lab has merged with 05-switch.
		fprintf(stdout, "NOTICE: received non-stp packet, forward it.\n");
		log(DEBUG, "the dst mac address is " ETHER_STRING ".\n", ETHER_FMT(eh->ether_dhost));
		
		iface_info_t* dest_iface = lookup_port(eh->ether_dhost); // 查找转发表
		if(dest_iface != NULL)
			iface_send_packet(dest_iface, packet, len);
		else 
			broadcast_packet(iface, packet, len);
		insert_mac_port(eh->ether_shost, iface);
		return ;
	}
	stp_port_handle_packet(iface->port, packet, len);
	free(packet);
}
```

另外还需要我们自己补全 `.py` 测试文件：

`four_node_ring.py` （已提供，节选）：

```python
class RingTopo(Topo):
    def build(self):
        b1 = self.addHost('b1')
        b2 = self.addHost('b2')
        b3 = self.addHost('b3')
        b4 = self.addHost('b4')

        self.addLink(b1, b2)
        self.addLink(b1, b3)
        self.addLink(b2, b4)
        self.addLink(b3, b4)

if __name__ == '__main__':
    check_scripts()

    topo = RingTopo()
    net = Mininet(topo = topo, controller = None) 

    for idx in range(4):
        name = 'b' + str(idx+1)
        node = net.get(name)
        clearIP(node)
        node.cmd('./scripts/disable_offloading.sh')
        node.cmd('./scripts/disable_ipv6.sh')

        # set mac address for each interface
        for port in range(len(node.intfList())):
            intf = '%s-eth%d' % (name, port)
            mac = '00:00:00:00:0%d:0%d' % (idx+1, port+1)

            node.setMAC(mac, intf = intf)

        # node.cmd('./stp > %s-output.txt 2>&1 &' % name)
        # node.cmd('./stp-reference > %s-output.txt 2>&1 &' % name)

    net.start()
    CLI(net)
    net.stop()
```

类比，自己设计了七个结点的测试文件 `seven_node.py` ：

```python
class MyTopo(Topo):
    def build(self):
        b1 = self.addHost('b1')
        b2 = self.addHost('b2')
        b3 = self.addHost('b3')
        b4 = self.addHost('b4')
        b5 = self.addHost('b5')
        b6 = self.addHost('b6')
        b7 = self.addHost('b7')
        
        self.addLink(b1, b2)
        self.addLink(b1, b3)
        self.addLink(b1, b4)
        
        self.addLink(b2, b5)
        self.addLink(b3, b6)
        self.addLink(b4, b7)
        
        self.addLink(b2, b3)
        self.addLink(b3, b4)
        
        self.addLink(b5, b6)
        self.addLink(b6, b7)

if __name__ == '__main__':
    check_scripts()

    topo = MyTopo()
    net = Mininet(topo = topo, controller = None) 

    for idx in range(7): # 注意此处要修改成自己设计的结点的个数
        name = 'b' + str(idx+1)
        node = net.get(name)
        clearIP(node)
        node.cmd('./scripts/disable_offloading.sh')
        node.cmd('./scripts/disable_ipv6.sh')

        # set mac address for each interface
        for port in range(len(node.intfList())):
            intf = '%s-eth%d' % (name, port)
            mac = '00:00:00:00:0%d:0%d' % (idx+1, port+1)

            node.setMAC(mac, intf = intf)

        # node.cmd('./stp > %s-output.txt 2>&1 &' % name)
        # node.cmd('./stp-reference > %s-output.txt 2>&1 &' % name)

    net.start()
    CLI(net)
    net.stop()
```

还有用于任务3测试的添加连个主机结点的网络拓扑结构 `four_node_add_host.py` ：

```python
class MyTopo(Topo):
    def build(self):
        b1 = self.addHost('b1')
        b2 = self.addHost('b2')
        b3 = self.addHost('b3')
        b4 = self.addHost('b4')
        h1 = self.addHost('h1')
        h2 = self.addHost('h2')

        self.addLink(b1, b2)
        self.addLink(b1, b3)
        self.addLink(b2, b4)
        self.addLink(b3, b4)
        self.addLink(h1, b2)
        self.addLink(h2, b4)

if __name__ == '__main__':
    check_scripts()

    topo = MyTopo()
    net = Mininet(topo = topo, controller = None) 

    h1, h2 = net.get('h1', 'h2')
    h1.cmd('ifconfig h1-eth0 10.0.0.1/8')
    h2.cmd('ifconfig h2-eth0 10.0.0.2/8')
    
    for h in [ h1, h2]:
        h.cmd('./scripts/disable_offloading.sh')
        h.cmd('./scripts/disable_ipv6.sh')
    
    
    for idx in range(4):
        name = 'b' + str(idx+1)
        node = net.get(name)
        clearIP(node)
        node.cmd('./scripts/disable_offloading.sh')
        node.cmd('./scripts/disable_ipv6.sh')

        # set mac address for each interface
        for port in range(len(node.intfList())):
            intf = '%s-eth%d' % (name, port)
            mac = '00:00:00:00:0%d:0%d' % (idx+1, port+1)

            node.setMAC(mac, intf = intf)

        # node.cmd('./stp > %s-output.txt 2>&1 &' % name)
        # node.cmd('./stp-reference > %s-output.txt 2>&1 &' % name)

    net.start()
    CLI(net)
    net.stop()
```

### 3. 启动脚本进行测试

（1）用 `four_node_ring.py` 测试

在 `06-stp` 目录下运行 `four_node_ring.py` ，开启 `b1` ， `b2` ， `b3` ， `b4` 四个结点，在每个结点输入命令 ==./stp > b*-output.txt 2>&1== ，这个命令会在结点运行 `stp` 程序，并将生成的结果输出到指定 `.txt` 文件。等到大概30秒后，新开一个终端，同样在 `06-stp` 目录下，输入命令 ==sudo pkill -SIGTERM stp==，强制所有 `stp` 程序保存最终状态并退出。最后在该目录下运行命令 ==./sump_output.sh *== 即可在终端查看结点的状态。

在进行生成树算法前，构建的网络拓扑结构为：

<img src="/Users/smx1228/Desktop/IMG_4326.jpg" alt="IMG_4326" style="zoom:10%;" />

生成树算法后，终端输出的结果为

<img src="/Users/smx1228/Desktop/截屏2021-04-21 下午4.49.42.png" alt="截屏2021-04-21 下午4.49.42" style="zoom:20%;" />

通过分析不难得出生成的结果：

<img src="/Users/smx1228/Desktop/IMG_4329.jpg" alt="IMG_4329" style="zoom:10%;" />

生成的拓扑结构满足我们的要求。

（2）用 `seven_node.py` 测试

和之前的操作类似，需要在 `b1` 至 `b7` 这七个结点运行stp程序，一分钟左右后关闭所有 `stp` 程序，查看结果。

一开始构建的拓扑结构为：

<img src="/Users/smx1228/Desktop/IMG_4328.jpg" alt="IMG_4328" style="zoom:10%;" />

运行stp程序后终端输出的信息为：

<center class="half"> 
	<img src="/Users/smx1228/Desktop/截屏2021-04-21 下午6.47.46.png" alt="截屏2021-04-21 下午6.47.46" style="zoom:12%;" /> 
	<img src="/Users/smx1228/Desktop/截屏2021-04-21 下午6.47.50.png" alt="截屏2021-04-21 下午6.47.50" style="zoom:12%;" />
</center>


整理后得知新建的网络拓扑结构为：

<img src="/Users/smx1228/Desktop/IMG_4324.jpg" alt="IMG_4324" style="zoom:10%;" />

（3）用 `four_node_add_host.py` 测试

在 `06-stp` 目录下运行 `four_node_add_host.py` ，开启 `b1` ， `b2` ， `b3` ， `b4` 四个结点，在每个结点输入命令 ==./stp > b*-output.txt 2>&1== 。等到大概30秒后（此时不能关闭正在运行的 `stp` 程序），另开 `h1` ， `h2` 两个结点，在 `h1` 结点运行命令 ==ping 10.0.0.2 -c 4==，在 `h2` 结点运行 ==ping 10.0.0.1 -c 4==，观察能否 `ping` 通。

一开始构建的拓扑结构为：

<img src="/Users/smx1228/Desktop/IMG_4327.jpg" alt="IMG_4327" style="zoom:10%;" />

`ping` 结果为：

<img src="/Users/smx1228/Desktop/截屏2021-04-22 上午11.13.38.png" alt="截屏2021-04-22 上午11.13.38" style="zoom:20%;" />

`h1` 和 `h2` 结点互相可以 `ping` 通，说明和数据包转发的结合也正确实现了。

## 实验总结与思考题

> 1. 调研说明标准生成树协议中，如何处理网络拓扑变动的情况：当结点加入时？当结点离开时？

当网络拓扑发生变化时，结点之间会通过一种新的 `BPDU` —— ==TCN BPDU== 进行交流。 `TCN BPDU` 是指在下游拓扑发生变化时向上游发送拓扑变化通知。 `TCN BPDU` 长度为4个字节，包含协议号、版本和类型。 `TCN BPDU` 由端口发出，发送条件为：当端口状态变为 `Forwarding` ，或端口状态从 `Forwarding` / `Learning` 变为 `Blocking` 。其中前者可视为有新结点加入，后者可视为有结点离开。

`TCN BPDU` 向上游发出后，会逐层传播到根结点，根结点于是发送配置 `BPDU` 告知所有结点。下游结点收到消息后会缩短其中存储的地址表的老化时间，从300秒减少至15秒，从而重新构建网络拓扑结构。整个重建过程需要20～50秒。

> 2. 调研说明标准生成树协议是如何在构建生成树过程中保持网络连通的？
>
>    提示：用不同的状态来标记每个端口，不同状态下允许不同的功能（Blocking, Listening, Learning, Forwarding等）

端口一共有五种状态，分别为：

`Disabled禁止状态` ：不收发任何报文。

`Blocking阻塞状态` ：不接收或者转发数据，接收、不发送 `BPDU` ，不进行地址学习。持续20秒后进入 `Listening状态` 。

`Listening侦听状态` ：不接收或者转发数据，接收、发送 `BPDU` ，不进行地址学习。持续15秒后进入 `Learning状态` 。

`Learning学习状态` ：不接收或者转发数据，接收、发送 `BPDU` ，开始进行地址学习。持续15秒后进入 `Forwarding状态` 。

`Forwarding转发状态` ：接收或者转发数据，接收、发送 `BPDU` ，进行地址学习。

后四种状态都可以接收 `BPDU` ，因此我们可以认为网络在构建生成树的过程是也可以进行信息交流。但是只有 `Forwarding状态` 可以接收和转发数据，也就是只有在 `Forwarding状态` 下，结点之间可以相互 `ping` 通，网络才是真正意义上的“连通”了，构建生成树过程中网络无法连通。

> 3. 实验中的生成树机制效率较低，调研说明快速生成树机制的原理

==快速生成树协议RSTP== 和生成树协议 `STP` 相比，主要的变化为：

（1）在某些情况下，处于 `Blocking状态` 的端口不必经历2倍的 `Forward Delay` 时延而可以直接进入 `Forwarding状态` ，如网络边缘端口（即直接与终端相连的端口），可以直接进入转发状态，不需要任何时延。因此 `RSTP` 中只设置了三种端口状态，分别为 `Discarding禁用状态` 、 `Learning学习状态` 和 `Forwarding转发状态` 。其中 `Discarding状态` 涵盖了之前的 `Disabled状态` 、 `Blocking状态` 和 `Listening状态` 。

（2）在 `RSTP` 中，端口类型有根端口和指定端口，这两个角色在 `RSTP` 中被保留，阻断端口分成备份端口和替换端口。备份端口为未使用的指定端口（处于丢弃状态），替换端口为未使用的根端口（处于丢弃状态）。当根端口/指定端口失效的情况下，替换端口/备份端口就会无时延地进入转发状态。

## 参考资料

1. 交换机网络的生成树协议简述：https://blog.csdn.net/Therock_of_lty/article/details/106027152
2. STP（生成树协议）研究（2）:STP拓扑计算、STP拓扑变化：https://blog.csdn.net/qq_40467699/article/details/107423773
3. STP理论02-BPDU(STP)：https://blog.51cto.com/u_9480916/2335426
4. STP 生成树协议 RSTP 快速生成树：https://www.cnblogs.com/centos2017/p/7896809.html