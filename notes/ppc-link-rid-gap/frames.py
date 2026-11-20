import struct,sys
N={1:"HELLO",2:"REQUEST",3:"RESPONSE",4:"DATA",5:"EOF",6:"ACK",7:"WINDOW",8:"WDATA",9:"STATUS",10:"RELEASE",11:"NOTIFY"}
lo,hi=int(sys.argv[2]),int(sys.argv[3])
for i,l in enumerate(open(sys.argv[1]),1):
    l=l.strip(); d='H>G' if l.startswith('> ') else 'G>H'; b=bytes.fromhex(l[2:] if d=='H>G' else l)
    if b[12:14]!=b'\x88\xb5': print(i,d,'non-link et',b[12:14].hex()); continue
    op,fl,ep,seq,ack,plen,rid,sess=struct.unpack('<BBHIIHIH',b[14:34]); p=b[34:34+plen]
    if not (lo<=rid<=hi or rid==0): continue
    txt=p[:48]
    if op in (2,3): txt=p[:80].decode('latin1').replace('\n','|')
    print(f'{i:4} {d} {N.get(op,op):8} fl={fl} sess={sess} ep={ep} rid={rid} seq={seq} ack={ack} plen={plen} {txt!r}' if op in (2,3,8) else f'{i:4} {d} {N.get(op,op):8} fl={fl} sess={sess} ep={ep} rid={rid} seq={seq} ack={ack} plen={plen} {p[:8].hex()}')
