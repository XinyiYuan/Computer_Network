# 数据包管理实验

<center> 中国科学院大学 </center>

<center>袁欣怡 2018K8009929021</center>

<center> 2021.4.26 </center>

## 实验内容

---

1. 重现 `BufferBloat` 问题

   （1）了解 `BufferBloat` 成因

   （2）使用 `reproduce_bufferbloat.py` 脚本复现 `BufferBloat` 过程

   （3）处理实验数据，解释现象

2. 解决 `BufferBloat` 问题

   （1）了解解决 `BufferBloat` 的几种方法

   （2）使用 `mitigate_bufferbloat.py` 脚本复现三种解决 `BufferBloat` 的方法

   （3）处理实验数据，画出对比图

3. 查阅资料解决思考题：拥塞控制机制对 `Bufferbloat` 的影响。

## 实验流程

---

### 1. 搭建实验环境

本实验中涉及到的文件主要有：


- code
   - reproduce_bufferbloat.py          # 重现Bufferbloat问题
   - mitigate_bufferbloat.py             # 解决BufferBloat问题
   - utils.py
   - utils.pyc
   - qlen-10                                     # 运行reproduce_bufferbloat.py，设置队列长度为10时的结果
      - cwnd.txt                             # cwnd结果
      - cwnd_handle.txt                  # cwnd.txt初步处理后结果
      - cwnd_handle.py                  # 对cwnd.txt进行初步处理，生成cwnd_handle.txt 
      - cwnd_figure.py                   # 将cwnd_handle.txt中数据绘制成图像
      - cwnd-time.png                    # 由cwnd_handle.txt绘制的图像
      - qlen.txt                             # qlen结果
      - qlen_figure.py                    # 将qlen.txt中数据绘制成图像
      - qlen-time.png                     # 由qlen.txt绘制的图像
      - rtt.txt                                # rtt结果
      - rtt_handle.txt                     # rtt.txt初步处理后结果
      - rtt_handle.py                      # 对rtt.txt进行初步处理，生成rtt_handle.txt 
      - rtt_figure.py                       # 将rtt_handle.txt中数据绘制成图像
      - rtt-time.png                        # 由rtt_handle.txt绘制的图像
   - qlen-50                                     # 运行reproduce_bufferbloat.py，设置队列长度为50时的结果
      - similar to qlen-10*
   - qlen-100                                   # 运行reproduce_bufferbloat.py，设置队列长度为100时的结果
      - similar to qlen-10*
   - codel                                      # 运行mitigate_bufferbloat.py，使用codel时的结果
      - rtt.txt                                # 原始测试结果
      - rtt_handle.py                     # 对rtt.txt进行初步处理，生成rtt_handle_codel.txt
      - rtt_handle_codel.txt            # rtt.txt初步处理后结果
   - red                                        # 运行mitigate_bufferbloat.py，使用redl时的结果
      - similar to codel*
   - taildrop                                  # 运行mitigate_bufferbloat.py，使用taildrop时的结果
      - similar to codel*
   - rtt_draw                                 # 对mitigate_bufferbloat.py的结果进行整合并作图
      - rtt_handle_codel.txt          # 从../codel下复制来的初步处理结果
      - rtt_handle_red.txt             # 从../red下复制来的初步处理结果
      - rtt_handle_taildrop.txt       # 从../taildrop下复制来的初步处理结果
      - rtt_draw.py                     # 将上面三个.txt文件中数据绘制成图像
      - rtt_after_mitigating_bufferbloat.png  # 由rtt_draw.py绘制的图像


### 2. 实验代码设计

（1）数据预处理： `cwnd_handle.py` 

```python
txt=open('cwnd.txt').readlines()
w=open('cwmd_handle.txt','w') 
for line in txt:
    line=line.replace(' 	 reno wscale:','')
    line=line.replace('9 rto:','')
    line=line.replace(' rtt:',',')
    line=line.replace('/',',')
    line=line.replace(' mss:',',')
    line=line.replace(' cwnd:',',')
    line=line.replace(' ssthresh:',',')
    line=line.replace(' bytes_acked:',',')
    line=line.replace(' segs_out:',',')
    line=line.replace(' segs_in:',',')
    line=line.replace(' send ',',')
    line=line.replace('Mbps','')
    line=line.replace(' lastsnd:',',')
    line=line.replace(' lastrcv:',',')
    line=line.replace(' lastack:',',')
    line=line.replace(' pacing_rate ',',')
    line=line.replace('Mbps','')
    line=line.replace(' unacked:',',')
    line=line.replace(' retrans:',',')
    line=line.replace('/',',')
    line=line.replace(' reordering:',',')
    line=line.replace(' lost:',',')
    line=line.replace(' sacked:',',')
    line=line.replace(' rcv_space:',',')
    w.write(line)
```

通过line.replace函数删除cwmd.txt中的文字字符，全部转化成数字。

（2）绘制图表： `cwnd_figure.py` 

```python
import matplotlib.pyplot as plt

data = open('cwmd_handle.txt','r').readlines()
para_1 = []
para_2 = []
for num in data:
    para_1.append(float(num.split(',')[0]))
    para_2.append(float(num.split(',')[6]))
plt.figure(dpi=100)
plt.title('qlen-10')
plt.plot(para_1,para_2)
plt.xlabel('time')
plt.ylabel('Cwnd')
plt.show()
```

从cwnd_handle.txt中读取数据到para_1和para_2中，然后作图并显示。

（3）绘制多组数据图表： `rtt_draw.py` 

```python
import matplotlib.pyplot as plt

f1 = open('rtt_handle_codel.txt')
data1 = f1.readlines()
para_1 = []
para_2 = []
for num in data1:
    para_1.append(float(num.split(',')[1]))
    para_2.append(float(num.split(',')[2]))

f2 = open('rtt_handle_red.txt')
data2 = f2.readlines()
para_3 = []
para_4 = []
for num in data2:
    para_3.append(float(num.split(',')[1]))
    para_4.append(float(num.split(',')[2]))

f3 = open('rtt_handle_taildrop.txt')
data3 = f3.readlines()
para_5 = []
para_6 = []
for num in data3:
    para_5.append(float(num.split(',')[1]))
    para_6.append(float(num.split(',')[2]))


plt.figure(dpi=100)
plt.title('rtt after mitigating bufferbloat')
plt.plot(para_1,para_2,color='green',label='codel')
plt.plot(para_3,para_4,color='red',label='red')
plt.plot(para_5,para_6,color='blue',label='taildrop')
plt.legend()
plt.ylim(0,1400)
plt.xlim(0,600)
plt.show()
```

读取三组数据，并绘制在一张图表上，用不同的颜色做标识。


### 3. 实验结果与分析

（1）重现 `BufferBloat` 问题

最大队列为10时的实验结果：

<img src="/Users/smx1228/Desktop/code/qlen-10/cwnd-time.png" alt="cwnd-time" style="zoom:15%;" />

<center> maxq=10时cwnd结果 </center>
<center> 稳定后，高点值约为34，低点值约为17，波动周期约为0.5～0.6s </center>

<img src="/Users/smx1228/Desktop/code/qlen-10/qlen-time.png" alt="qlen-time" style="zoom:15%;" />

<center> maxq=10时qlen结果 </center>
<center> 稳定后，高点值为10，低点值为1，波动周期约为0.5～0.6s </center>

<img src="/Users/smx1228/Desktop/code/qlen-10/rtt-time.png" alt="rtt-time" style="zoom:15%;" />

<center> maxq=10时rtt结果 </center>
<center> 稳定后，高点值约为40~45，低点值约为22，波动周期约为13s </center>

最大队列为50时的实验结果：

<img src="/Users/smx1228/Desktop/code/qlen-50/cwnd-time.png" alt="cwnd-time" style="zoom:15%;" />

<center> maxq=50时cwnd结果 </center>
<center> 稳定后，高点值约为100，低点值约为50，波动周期约为7～8s </center>

<img src="/Users/smx1228/Desktop/code/qlen-50/qlen-time.png" alt="qlen-time" style="zoom:15%;" />

<center> maxq=50时qlen结果 </center>
<center> 稳定后，高点值约为50，低点值约为22，波动周期约为7～8s </center>

<img src="/Users/smx1228/Desktop/code/qlen-50/rtt-time.png" alt="rtt-time" style="zoom:15%;" />

<center> maxq=50时rtt结果 </center>
<center> 稳定后，高点值约为130，低点值约为70，波动周期约为7～8s </center>

最大队列为100时的实验结果：

<img src="/Users/smx1228/Desktop/code/qlen-100/cwnd-time.png" alt="cwnd-time" style="zoom:15%;" />

<center> maxq=100时cwnd结果 </center>
<center> 稳定后，高点值约200，低点值约为100，波动周期约为20～25s </center>

<img src="/Users/smx1228/Desktop/code/qlen-100/qlen-time.png" alt="qlen-time" style="zoom:15%;" />

<center> maxq=100时qlen结果 </center>
<center> 稳定后，高点值约100，低点值约为45，波动周期约为20～25s </center>

<img src="/Users/smx1228/Desktop/code/qlen-100/rtt-time.png" alt="rtt-time" style="zoom:15%;" />

<center> maxq=100时rtt结果 </center>
<center> 稳定后，高点值约200，低点值约为100，波动周期约为20～25s </center>

实验分析：

`BufferBloat` 问题是指数据包在队列中存留时间过长引起的延迟过大问题[Gettys2011]。我们可以结合下面一张图来分析。对于图中的蓝色部分，吞吐率没有达到 `BDP` ，线路没有被填满，因此排队时延为0， `rtt` 为传播过程中处理数据包所需要的时间（传播时延），可以稳定地维持在较低水平。对于图中的绿色部分，线路已经被填满，没有立刻被转发的数据包会进入缓冲区等待，此时 `rtt` 为传播时延和排队时延之和，会随着发送数据包的数目增加而增大。在这个阶段，增加发送的数据包的数目不会提高吞吐率，只会导致 `rtt` 增加，这就是 `BufferBloat` 问题。

<img src="/Users/smx1228/Desktop/截屏2021-04-28 下午6.58.00.png" alt="截屏2021-04-28 下午6.58.00" style="zoom:30%;" />

我们复现的折线图中测量了三种数据： `cwnd` ， `qlen` 和 `rtt` 。以下分别进行解释和分析：

-  `cwnd` ：拥塞窗口 `Congestion Window` 。

为了在网络通信中防止拥塞，发送方会维持一个叫做拥塞窗口的可滑动窗口。拥塞窗口的大小取决于网络的拥塞程度，并且会动态地发生变化。拥塞窗口这个概念在慢启动算法中被提出。慢启动算法的思路是，一开始发送少量数据，探测网络的拥塞程度。如果网络上的数据包较少，那么就将拥塞窗口的大小翻一倍，以便发送更多的数据。

但以指数的方式扩大 `cwnd` 会导致增大速度过快，因此在 `cwnd` 达到某个临界值时，会从指数增长切换为线性增长。这个临界值被称为 `ssthresh` ，这种线性增长的算法即为拥塞避免算法。

另外，无论是在慢启动阶段还是拥塞避免阶段，只要发送方判断网络出现拥塞，就会把 `ssthresh` 设置为之前的一半， `cwnd` 设置为1，重新进行慢启动算法。因此， `cwnd` 的变化曲线大致如下图所示：

<img src="/Users/smx1228/Desktop/截屏2021-04-28 下午7.37.24.png" alt="截屏2021-04-28 下午7.37.24" style="zoom:30%;" />

从之前的实验结果截图中可以看出：1.  `cwnd` 的变化曲线与理论结果基本相同。2.  `cwnd` 波动的周期与 `maxq` 的平方成正比。 `maxq=10` 时周期约为0.5～0.6s， `maxq=50` 时周期约为7～8s， `maxq=100` 时周期约为20～25s

-  `qlen` ：队列长度 `Queue Length` ，表示缓冲区中当前的缓存的个数，也即正在排队等待发送的队列长度。

观察之前的实验结果可以发现：起初缓存区为空， `qlen=0` ，然后发送端采用慢启动方式增大发送的数据包大小，导致 `qlen` 不断增大，直到 `qlen` 等于 `maxq` 。 `qlen=maxq` 时会发生丢包，因此需要降低发包的速率，降低丢包率。这个过程会不断重复，如图中所示。

-  `rtt` ：往返时延 `Round-Trip Time` ，从发送方发送数据开始到发送方收到接收方的确认时间总共经历的时延。

观察之前的实验结果，我们不难发现：起初缓存区为空，则发送端采用慢启动方式增大发送的数据包大小，当单位时间发送量还未达到 `BDP` 的时候， `rtt` 维持在较低水平。随着发送速率的增大， `qlen` 增大，数据包排队时延增加，因此 `rtt` 也增加，直到 `qlen` 接近 `maxq` 时， `rtt` 的值到达最大值，同时发生丢包，导致有些数据包的 `rtt` 非常大。意识到丢包后，发送端会降低发送速率，让网络恢复到比较通畅的状态。这个过程也会不断重复，如图所示。

同时还发现， `cwnd` 、 `qlen` 、 `rtt` 的变化趋势是同步的，这也是符合理论预期的。

（2）解决 `BufferBloat` 问题

<img src="/Users/smx1228/Desktop/code/rtt_draw/rtt_after_mitigating_bufferbloat.png" alt="rtt_after_mitigating_bufferbloat" style="zoom:15%;" />

在解决 `BufferBloat` 问题上，实验中使用了三种算法： `TailDrop` ， `RED` 和 `CoDel` 。

-  `TailDrop` ：尾部丢弃算法，当队列满时，将新到达的数据包丢弃。此算法实现简单，也不需要任何参数，但是因为等缓冲区满了才开始丢包，此时网络状态已经非常拥塞，所以没有主动避免拥塞的功能。

-  `RED` ：随机早期检测算法 `Random Early Detection` 。在此算法中，队列满之前就开始主动地概率性丢包，丢包概率与队列长度正相关。此算法可能提前处理可能出现的拥塞情况，但是由于需要设定的参数很多，且调参比较困难，所以实际实现没法达到理想的效果。

-  `CoDel` ：控制时延算法 `Controlled Delay` 。 此算法会控制数据包在队列中的时间并以此为度量指标（而不是队列长度）。具体来说，当包停留时间超过 `target` 值时，将该数据包丢弃，并且设置下次丢包时间，直到所有包的停留时间都小于 `target` 值。此算法可以提前处理拥塞，而且需要的参数个数比 `RED` 算法少，降低了实际实现的难度。

观察实验结果（实验中为了获得更多信息，修改了运行时间，从60秒改为200秒）：

- 一开始网络处于较为通畅的状态， `rtt` 维持在较低的水平，三种算法此时还没有发挥作用。后来，链路带宽降低，大量数据包堆积。
-  `TailDrop` 算法难以应对大量涌入的数据包，大量数据包被直接丢弃， `rtt` 迅速飙升，随后发送端察觉到丢包，开始降低发送速率， `rtt` 下降，但还是维持在比较高的水平。
-  `RED` 算法相比 `TailDrop` 算法效果有显著提升，因为 `RED` 算法会根据队列长度进行丢包，让队列长度维持在较低的水平。但是由于调参的复杂性， `RED` 算法在某些带宽下效果不明显，甚至不如 `TailDrop` 算法。
-  `CoDel` 算法可以将 `rtt` 维持在较稳定且较低的水平，因为 `CoDel` 算法能比 `RED` 算法更早让发送端意识到拥堵，所以 `CoDel` 算法可以更快清空信道，降低 `rtt` 的波动幅度。

## 实验总结与思考题

---

（1）实验总结

- 通过本次实验，对网络结点中的数据包缓冲区的工作方式有了更深刻的认识，了解了几种常用的算法的工作方式。

- 学习了更多用 `python` 处理实验数据和作图的方法和技巧。

（2）思考题

拥塞控制机制对 `Bufferbloat` 的影响：前文中提到，导致 `Bufferbloat` 问题的三个关键因素：队列长度，队列管理机制，和拥塞控制机制。同时，分别从上述三个角度都可以解决 `Bufferbloat` 问题。调研分析两种新型拥塞控制机制（ `BBR [Cardwell2016]` , `HPCC [Li2019]` ），阐述其是如何解决 `Bufferbloat` 问题的。

-  `BBR` ：

  `BBR` 会实时检测当前的传输带宽，从而据此计算接下来的发送速率。具体来说，我们知道， `rtt` 越小，说明当前链路上排队的数据越少，当链路队列被清空时，排队时延=0， `rtt` = `min_rtt` =传播时延。BBR使用 `min_rtt` 是否可以维持来判断当前的传输速率是否能保证不排队。但需要注意的是， `BBR` 无法保证不会排队，但是它会根据 `rtt` 来调整数据的发送速率。

-  `HPCC` ：

  `HPCC` ，高精度拥塞控制 `High Precision Congestion Control` ，核心理念是利用精确链路负载信息直接计算合适的发送速率，而不是像现有的拥塞控制算法一样迭代地探索合适的速率，且 `HPCC` 速率更新由数据包的 `ACK` 驱动，而不是靠定时器驱动。

## 参考资料

---

1. python之parser.add_argument()用法——命令行选项、参数和子命令解析器：https://blog.csdn.net/qq_34243930/article/details/106517985
2. python如何使用Matplotlib画图（基础篇）：https://zhuanlan.zhihu.com/p/109245779
3. TCP拥塞控制：https://blog.csdn.net/sicofield/article/details/9708383
4. TCP核心概念-慢启动，ssthresh，拥塞避免，公平性的真实含义：https://blog.csdn.net/dog250/article/details/51439747
5. TSQ/CoDel队列管理以及TCP BBR如何解决Bufferbloat问题：https://blog.csdn.net/dog250/article/details/72849893
6. BBR, the new kid on the TCP block：https://blog.apnic.net/2017/05/09/bbr-new-kid-tcp-block/#article-content
7. LinuxKernel_4.9中TCP_BBR算法的科普解释：https://zhuanlan.zhihu.com/p/103766867
8. 改进 TCP，阿里提出高速云网络拥塞控制协议 HPCC：https://www.infoq.cn/article/q-J1qOtjcDUmYDCbWTB3