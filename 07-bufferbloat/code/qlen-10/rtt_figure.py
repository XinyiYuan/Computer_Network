import matplotlib.pyplot as plt

data = open('rtt_handle.txt','r').readlines()
para_1 = []
para_2 = []
for num in data:
    para_1.append(float(num.split(',')[0]))
    para_2.append(float(num.split(',')[1]))
plt.figure(dpi=100)
plt.title('qlen-10')
plt.plot(para_1,para_2)
plt.xlabel('time')
plt.ylabel('Rtt')
plt.show()