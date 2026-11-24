#!/usr/bin/env python3
# Prints a binary as Apple II monitor hex lines ("ADDR: XX XX ...", CR-separated).
import sys

path, start = sys.argv[1], int(sys.argv[2], 16)
data = open(path, 'rb').read()
lines = []
for off in range(0, len(data), 8):
    chunk = data[off:off + 8]
    lines.append('%04X: %s' % (start + off, ' '.join('%02X' % b for b in chunk)))
sys.stdout.write('\r'.join(lines) + '\r')
