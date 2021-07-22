txt=open('rtt.txt').readlines()
w=open('rtt_handle_red.txt','w')
i=1;
for line in txt:
    line=line.replace(' 64 bytes from 10.0.2.22: icmp_seq=1 ttl=63 time',str(i))
    line=line.replace('=',',')
    line=line.replace(' ms','')
    i=i+1
    w.write(line)