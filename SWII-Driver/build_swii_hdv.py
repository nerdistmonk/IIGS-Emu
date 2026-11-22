#!/usr/bin/env python3
"""Build an 800K ProDOS volume holding the one-byte patched StyleWriter driver.

Usage:
  python3 build_swii_hdv.py "<path to System 604.hdv>" [output.hdv]

The source must be the System 6 disk image whose SHA-256 is SRC_SHA. The output
defaults to SWII-Driver.hdv next to this script. The image contains Apple's
copyrighted driver, so it is gitignored and must not be committed.

Layout (512-byte blocks):
  0-1  boot (zero)            2  volume directory
  3    /SYSTEM directory      4  /SYSTEM/DRIVERS directory
  5    STYLEWRITER index      6  volume bitmap
  7-46 STYLEWRITER data (40 blocks, EOF 20047)
  47-1599 free
Directory headers and entries are cloned from the source System 6 disk and then
re-pointed at the new blocks, so date/type/aux fields match the original.
"""
import hashlib
import os
import struct
import sys

SRC_SHA = '61e9b40627f2345ce61efebf98dc986b5aa0cb7935020e2bec7dcb6d65794f2e'
PATCH_OFF = 0x474D
HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_OUT = os.path.join(HERE, 'SWII-Driver.hdv')

TOTAL_BLOCKS = 1600
VOL_NAME = b'SWIIDRIVER'
SRC_VOL, SRC_SYSTEM, SRC_DRIVERS = 2, 43, 47
VOL_DIR, SYS_DIR, DRV_DIR, SW_INDEX, BITMAP = 2, 3, 4, 5, 6
DATA_START = 7
DRIVER_LEN = 20047
DATA_BLOCKS = (DRIVER_LEN + 511) // 512  # 40
SW_BLOCKS_USED = DATA_BLOCKS + 1  # plus index block
BLK = 512


def block(img, n):
    return img[n * BLK:(n + 1) * BLK]


def entries(img, key):
    first = True
    blk = key
    seen = set()
    while blk and blk not in seen:
        seen.add(blk)
        b = block(img, blk)
        for i in range(13):
            off = 4 + i * 39
            e = b[off:off + 39]
            if first and i == 0:
                continue
            if e[0] == 0:
                continue
            yield e
        blk = struct.unpack_from('<H', b, 2)[0]
        first = False


def parse(e):
    st = e[0] >> 4
    nl = e[0] & 0x0F
    name = e[1:1 + nl].decode('latin-1', 'replace')
    ftype = e[0x10]
    key = struct.unpack_from('<H', e, 0x11)[0]
    blocks = struct.unpack_from('<H', e, 0x13)[0]
    eof = e[0x15] | (e[0x16] << 8) | (e[0x17] << 16)
    aux = struct.unpack_from('<H', e, 0x1F)[0]
    return st, name, ftype, key, blocks, eof, aux


def file_data(img, st, key, eof):
    out = bytearray()
    if st == 1:
        out += block(img, key)
    elif st == 2:
        idx = block(img, key)
        for i in range(256):
            p = idx[i] | (idx[256 + i] << 8)
            out += block(img, p) if p else b'\0' * BLK
    else:
        raise SystemExit('unsupported storage type %d' % st)
    return bytes(out[:eof])


def find_entry(img, dir_key, name):
    for e in entries(img, dir_key):
        if parse(e)[1] == name:
            return bytearray(e)
    raise SystemExit('entry %s not found in dir %d' % (name, dir_key))


def subdir_header(template, name, count, parent_blk, parent_entry):
    h = bytearray(template)
    h[0] = 0xE0 | len(name)
    h[1:16] = name.ljust(15, b'\0')
    struct.pack_into('<H', h, 33, count)
    struct.pack_into('<H', h, 35, parent_blk)
    h[37] = parent_entry
    return h


def set_entry(template, key, blocks, eof, header_blk):
    e = bytearray(template)
    struct.pack_into('<H', e, 0x11, key)
    struct.pack_into('<H', e, 0x13, blocks)
    e[0x15:0x18] = eof.to_bytes(3, 'little')
    struct.pack_into('<H', e, 0x25, header_blk)
    return e


def main():
    if len(sys.argv) not in (2, 3):
        raise SystemExit(__doc__)
    src_path = sys.argv[1]
    out_path = sys.argv[2] if len(sys.argv) == 3 else DEFAULT_OUT

    raw = open(src_path, 'rb').read()
    if hashlib.sha256(raw).hexdigest() != SRC_SHA:
        raise SystemExit('source disk sha256 mismatch: expected System 604.hdv')

    sw_entry = find_entry(raw, SRC_DRIVERS, 'STYLEWRITER')
    st, _, _, sw_key, _, sw_eof, _ = parse(sw_entry)
    orig = file_data(raw, st, sw_key, sw_eof)
    if sw_eof != DRIVER_LEN or len(orig) != DRIVER_LEN:
        raise SystemExit('unexpected driver length')

    patched = bytearray(orig)
    if patched[PATCH_OFF - 2:PATCH_OFF + 1] != bytes([0x29, 0x02, 0xD9]):
        raise SystemExit('expected AND #$D902 at patch site')
    patched[PATCH_OFF] = 0x59

    sys_entry = find_entry(raw, SRC_VOL, 'SYSTEM')
    drv_entry = find_entry(raw, SRC_SYSTEM, 'DRIVERS')
    vol_hdr = bytearray(block(raw, SRC_VOL)[4:43])
    sys_hdr_t = block(raw, SRC_SYSTEM)[4:43]
    drv_hdr_t = block(raw, SRC_DRIVERS)[4:43]

    vol_hdr[0] = 0xF0 | len(VOL_NAME)
    vol_hdr[1:16] = VOL_NAME.ljust(15, b'\0')
    struct.pack_into('<H', vol_hdr, 33, 1)
    struct.pack_into('<H', vol_hdr, 35, BITMAP)
    struct.pack_into('<H', vol_hdr, 37, TOTAL_BLOCKS)

    img = bytearray(TOTAL_BLOCKS * BLK)

    def put(n, off, data):
        base = n * BLK + off
        img[base:base + len(data)] = data

    put(VOL_DIR, 4, vol_hdr)
    put(VOL_DIR, 4 + 39, set_entry(sys_entry, key=SYS_DIR, blocks=1,
                                   eof=512, header_blk=VOL_DIR))

    put(SYS_DIR, 4, subdir_header(sys_hdr_t, b'SYSTEM', 1, VOL_DIR, 2))
    put(SYS_DIR, 4 + 39, set_entry(drv_entry, key=DRV_DIR, blocks=1,
                                   eof=512, header_blk=SYS_DIR))

    put(DRV_DIR, 4, subdir_header(drv_hdr_t, b'DRIVERS', 1, SYS_DIR, 2))
    put(DRV_DIR, 4 + 39, set_entry(sw_entry, key=SW_INDEX,
                                   blocks=SW_BLOCKS_USED, eof=DRIVER_LEN,
                                   header_blk=DRV_DIR))

    # sapling index: lo bytes at [0..255], hi bytes at [256..511]
    idx = bytearray(512)
    for i in range(DATA_BLOCKS):
        n = DATA_START + i
        idx[i] = n & 0xFF
        idx[256 + i] = n >> 8
    put(SW_INDEX, 0, idx)

    for i in range(DATA_BLOCKS):
        put(DATA_START + i, 0, patched[i * BLK:(i + 1) * BLK])

    # bitmap: 1 = free, 0 = in use
    bm = bytearray(b'\xff' * (TOTAL_BLOCKS // 8))
    for b in range(DATA_START + DATA_BLOCKS):
        bm[b // 8] &= ~(0x80 >> (b % 8)) & 0xFF
    put(BITMAP, 0, bm)

    with open(out_path, 'wb') as f:
        f.write(img)

    readback = file_data(bytes(img), 2, SW_INDEX, DRIVER_LEN)
    if readback != bytes(patched):
        raise SystemExit('read-back of written STYLEWRITER does not match patched bytes')

    diffs = [i for i in range(DRIVER_LEN) if orig[i] != patched[i]]
    print('wrote', out_path, len(img), 'bytes')
    print('sha256', hashlib.sha256(img).hexdigest())
    print('driver byte diffs vs original:', [hex(i) for i in diffs])


if __name__ == '__main__':
    main()
