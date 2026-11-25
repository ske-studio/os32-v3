"""Read the FAT12 and D88 images produced for distribution."""

def fat12_files(path):
    """FAT12 イメージのファイルを {PATH (大文字): bytes} と空きバイトで返す

    BPB (オフセット 0x0B) から配置を読む。mkfat12 が作る 2HD (1024B/セクタ) と
    1.44MB (512B/セクタ) の両方。. / .. と削除済みは飛ばす。
    """
    img = open(path, 'rb').read()
    import struct
    bps, spc, rsv, nfat, nroot, tot, _media, fatsz = \
        struct.unpack_from('<HBHBHHBH', img, 0x0B)
    fat = img[rsv * bps:(rsv + fatsz) * bps]
    root_off = (rsv + nfat * fatsz) * bps
    root_secs = (nroot * 32 + bps - 1) // bps
    data_sec = rsv + nfat * fatsz + root_secs
    nclus = (tot - data_sec) // spc
    csize = bps * spc

    def nxt(c):
        v = fat[c * 3 // 2] | (fat[c * 3 // 2 + 1] << 8)
        return (v >> 4) if c & 1 else (v & 0xFFF)

    def chain(c):
        out = []
        while 2 <= c < 0xFF8 and len(out) <= nclus:
            out.append(c)
            c = nxt(c)
        return out

    def read_chain(c):
        return b''.join(img[(data_sec + (x - 2) * spc) * bps:][:csize] for x in chain(c))

    files = {}

    def walk(raw, prefix):
        for i in range(0, len(raw) - 31, 32):
            e = raw[i:i + 32]
            if e[0] == 0:
                break
            if e[0] == 0xE5 or e[11] == 0x0F or e[0] == ord('.'):
                continue
            base = e[0:8].decode('ascii').rstrip()
            ext = e[8:11].decode('ascii').rstrip()
            name = base + ('.' + ext if ext else '')
            clus = struct.unpack_from('<H', e, 26)[0]
            size = struct.unpack_from('<I', e, 28)[0]
            if e[11] & 0x10:
                walk(read_chain(clus), prefix + '/' + name)
            elif not e[11] & 0x08:
                files[prefix + '/' + name] = read_chain(clus)[:size] if size else b''

    walk(img[root_off:root_off + nroot * 32], '')
    used = sum(1 for c in range(2, nclus + 2) if nxt(c) != 0)
    return files, (nclus - used) * csize, nclus * csize


def d88_to_raw(blob):
    """D88 を読み、(c, h, r) 順に並べたセクタ列を RAW として返す

    ヘッダ 0x2B0 (名前 17 + 予約 9 + 保護 1 + 種別 1 + 全長 4 + トラック表
    164 × 4)。各セクタは 16 バイトのヘッダ (C, H, R, N, 数, 密度, 削除, 状態,
    予約 5, データ長 2) + データ。トラックはシリンダ × 2 + ヘッド の順で読む。
    形が崩れていれば ValueError。
    """
    import struct
    if len(blob) < 0x2B0:
        raise ValueError('D88 のヘッダより短い')
    disk_size = struct.unpack_from('<I', blob, 0x1C)[0]
    if disk_size != len(blob):
        raise ValueError(f'ヘッダの全長 {disk_size} != 実物 {len(blob)}')
    offs = struct.unpack_from('<164I', blob, 0x20)
    out = bytearray()
    for t, off in enumerate(offs):
        if off == 0:
            continue
        p = off
        # 1 本目のセクタのヘッダが「このトラックのセクタ数」を持つ。その数だけ読む
        if p + 16 > len(blob):
            raise ValueError(f'トラック {t}: セクタのヘッダが範囲外')
        nsec = struct.unpack_from('<H', blob, p + 4)[0]
        secs = {}
        for _ in range(nsec):
            if p + 16 > len(blob):
                raise ValueError(f'トラック {t}: セクタのヘッダが範囲外')
            c, h, r, n, ns, _dens, _dele, _st = struct.unpack_from('<BBBBHBBB', blob, p)
            dlen = struct.unpack_from('<H', blob, p + 14)[0]
            if ns != nsec:
                raise ValueError(f'トラック {t} R{r}: セクタ数 {ns} != {nsec}')
            if (c, h) != (t // 2, t % 2):
                raise ValueError(f'トラック {t}: C/H = {c}/{h}')
            if dlen != 128 << n:
                raise ValueError(f'トラック {t} R{r}: データ長 {dlen} と N={n} が合わない')
            if r in secs:
                raise ValueError(f'トラック {t}: R{r} が 2 回')
            secs[r] = blob[p + 16:p + 16 + dlen]
            p += 16 + dlen
        # R は重複なく 1〜spt (並べ替えで壊れた R を隠さない)
        if sorted(secs) != list(range(1, nsec + 1)):
            raise ValueError(f'トラック {t}: R が 1〜{nsec} でない ({sorted(secs)})')
        for r in sorted(secs):
            out += secs[r]
    return bytes(out)
