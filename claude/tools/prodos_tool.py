# Usage: prodos_tool.py IMAGE CMD [ARGS]. Reads a ProDOS .po/.hdv image.
# Commands: ls | get NAME OUTFILE | find PATTERN | tools NAME... | scan | scrap | le
# scan, scrap, and le look for Toolbox calls (JSL $E10000) in file bodies.
import sys

data = open(sys.argv[1], 'rb').read()
BS = 512


def blk(n):
    return data[n * BS:(n + 1) * BS]


def le16(b, o):
    return b[o] | (b[o + 1] << 8)


def le24(b, o):
    return b[o] | (b[o + 1] << 8) | (b[o + 2] << 16)


def read_file(key, st, eof):
    out = bytearray()
    if st == 1:
        out += blk(key)
    elif st == 2:
        idx = blk(key)
        for i in range(256):
            p = idx[i] | (idx[256 + i] << 8)
            out += blk(p) if p else bytes(BS)
    elif st == 3:
        master = blk(key)
        for j in range(256):
            p = master[j] | (master[256 + j] << 8)
            if not p:
                out += bytes(BS * 256)
                continue
            idx = blk(p)
            for i in range(256):
                q = idx[i] | (idx[256 + i] << 8)
                out += blk(q) if q else bytes(BS)
    return bytes(out[:eof])


def walk(key, prefix, first, out):
    while key:
        b = blk(key)
        for i in range(1 if first else 0, 13):
            e = b[4 + i * 39: 4 + (i + 1) * 39]
            st = e[0] >> 4
            nl = e[0] & 0x0F
            if st == 0:
                continue
            name = prefix + '/' + e[1:1 + nl].decode('latin1')
            if st == 0x0D:
                out.append(dict(name=name, dir=True))
                walk(le16(e, 0x11), name, True, out)
            else:
                out.append(dict(name=name, dir=False, st=st, ftype=e[0x10],
                                key=le16(e, 0x11), eof=le24(e, 0x15),
                                aux=le16(e, 0x1F)))
        first = False
        key = le16(b, 2)


entries = []
walk(2, '', True, entries)

cmd = sys.argv[2]
if cmd == 'ls':
    for f in entries:
        if f['dir']:
            print('DIR  ' + f['name'])
        else:
            print('%-60s type=$%02X aux=$%04X eof=%d' % (f['name'], f['ftype'], f['aux'], f['eof']))
elif cmd == 'scan':
    for f in entries:
        if f['dir'] or f['ftype'] not in (0xB3, 0xB5, 0xB6, 0xBA, 0xB9):
            continue
        d = read_file(f['key'], f['st'], f['eof'])
        jsl = d.count(b'\x22\x00\x00\xe1')
        gs = d.count(b'\xa2\x16\x0d')
        if jsl:
            print('%-60s jsl_e10000=%d ldx_0d16=%d' % (f['name'], jsl, gs))
elif cmd == 'find':
    pat = sys.argv[3].encode('latin1')
    for f in entries:
        if f['dir']:
            continue
        d = read_file(f['key'], f['st'], f['eof'])
        n = d.count(pat)
        if n:
            print('%-60s type=$%02X count=%d' % (f['name'], f['ftype'], n))
elif cmd == 'get':
    want = sys.argv[3]
    for f in entries:
        if not f['dir'] and f['name'] == want:
            open(sys.argv[4], 'wb').write(read_file(f['key'], f['st'], f['eof']))
            print('wrote %d bytes type=$%02X aux=$%04X' % (f['eof'], f['ftype'], f['aux']))
            break
    else:
        print('not found')
if cmd == 'tools':
    from collections import Counter
    for f in entries:
        if f['dir'] or f['name'].split('/')[-1] not in sys.argv[3:]:
            continue
        d = read_file(f['key'], f['st'], f['eof'])
        c = Counter()
        i = d.find(b'\x22\x00\x00\xe1')
        while i >= 0:
            w = d[max(0, i - 24):i]
            for k in range(len(w) - 2):
                if w[k] == 0xA2:
                    c['%04X' % (w[k + 1] | (w[k + 2] << 8))] += 1
            i = d.find(b'\x22\x00\x00\xe1', i + 1)
        print(f['name'], dict(c.most_common(12)))
if cmd == 'scrap':
    from collections import Counter
    tot = Counter()
    for f in entries:
        if f['dir'] or f['ftype'] not in (0xB3, 0xB5, 0xB6, 0xB9, 0xBA, 0xB8, 0xBB, 0x06, 0xB0, 0xFF, 0xF9):
            continue
        d = read_file(f['key'], f['st'], f['eof'])
        c = Counter()
        i = d.find(b'\x22\x00\x00\xe1')
        while i >= 0:
            w = d[max(0, i - 24):i]
            for k in range(len(w) - 2):
                if w[k] == 0xA2:
                    v = '%04X' % (w[k + 1] | (w[k + 2] << 8))
                    if v[2:] == '16':
                        c[v] += 1
            i = d.find(b'\x22\x00\x00\xe1', i + 1)
        if c:
            tot.update(c)
            print(f['name'], dict(c))
    print('TOTAL', dict(tot))
if cmd == 'le':
    from collections import Counter
    for f in entries:
        if f['dir'] or f['ftype'] not in (0xB3, 0xB5, 0xB6, 0xB9, 0xBA, 0xB8, 0xBB, 0x06, 0xB0, 0xFF, 0xF9):
            continue
        d = read_file(f['key'], f['st'], f['eof'])
        c = Counter()
        i = d.find(b'\x22\x00\x00\xe1')
        while i >= 0:
            w = d[max(0, i - 24):i]
            for k in range(len(w) - 2):
                if w[k] == 0xA2:
                    v = '%04X' % (w[k + 1] | (w[k + 2] << 8))
                    if v[2:] == '14' or v == '0D16':
                        c[v] += 1
            i = d.find(b'\x22\x00\x00\xe1', i + 1)
        if c:
            print(f['name'], dict(c))
