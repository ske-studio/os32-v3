import base64, json, struct, sys
a, b = (json.load(open(p)) for p in sys.argv[1:])
assert all(r['ok'] and r['rate'] == 44100 for r in (a,b))
end = a['frames_total']
start = b['frames_total'] - b['frames']
assert start <= end < b['frames_total'], 'capture gap or counter reset'
raw = base64.b64decode(b['pcm'], validate=True)
assert len(raw) == b['frames'] * 4
fresh = struct.iter_unpack('<hh', raw[4*(end-start):])
marks = [(i,(r-10000)*256) for i,(_,r) in enumerate(fresh) if r]
assert len(marks) >= 2 and all(0 <= frame < 1323000 for _,frame in marks)
assert all(j-i == 256 and g-f == 256 for (i,f),(j,g) in zip(marks,marks[1:]))
print('continued audio through application frame', marks[-1][1])
