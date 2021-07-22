txt=open('rtt.txt').readlines()
w=open('rtt_handle.txt','w') 
for line in txt:
    line=line.replace(' 64 bytes from 10.0.2.22: icmp_seq=1 ttl=63 time=','')
    line=line.replace(' ms','')
    w.write(line)