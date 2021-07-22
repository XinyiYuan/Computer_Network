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
