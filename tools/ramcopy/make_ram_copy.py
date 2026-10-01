#!/usr/bin/env python3
"""
Makes a RAM copy of an application under its own database name and creator.

Why: an application in ROM cannot be patched (src/apppatch.c writes into the
stored code), and a RAM copy with the same name and creator does not reliably
win the launch against the ROM version (tested on a Visor: the ROM version kept
running, also with a higher database version). A copy with its own creator
shows up as a second icon in the launcher and runs, patched, from RAM. It keeps
using the original's databases, which the code opens by its compiled-in creator
and name; alarms and the hardware button still start the ROM version.

usage: make_ram_copy.py in.prc out.prc NAME CREATOR LABEL
  NAME     database name of the copy (up to 31 characters)
  CREATOR  four characters
  LABEL    launcher name (at most as long as the original's 'tAIN 1000')
"""
import struct, sys, time

def main():
    src, dst, name, creator, label = sys.argv[1:6]
    d = bytearray(open(src, 'rb').read())
    n = struct.unpack('>H', d[76:78])[0]
    ents = [(bytes(d[78 + i * 10:82 + i * 10]).decode('latin1'),
             struct.unpack('>H', d[82 + i * 10:84 + i * 10])[0],
             struct.unpack('>I', d[84 + i * 10:88 + i * 10])[0]) for i in range(n)]
    ends = [e[2] for e in ents[1:]] + [len(d)]
    tain = [(o, e) for (t, i, o), e in zip(ents, ends) if t == 'tAIN' and i == 1000]
    if not tain:
        sys.exit("no tAIN 1000 (launcher name) in " + src)
    off, end = tain[0]
    room = d[off:end].index(b'\0') + 1 if b'\0' in d[off:end] else 0
    if len(label) + 1 > room:
        sys.exit("label longer than the original (%d characters)" % (room - 1))
    d[off:off + room] = label.encode('latin1').ljust(room, b'\0')
    if len(name) > 31 or len(creator) != 4:
        sys.exit("name up to 31 characters, creator exactly four")
    d[0:32] = name.encode('latin1').ljust(32, b'\0')
    d[64:68] = creator.encode('latin1')
    now = int(time.time()) + 2082844800            # Palm epoch 1904
    struct.pack_into('>II', d, 36, now, now)       # created, modified
    open(dst, 'wb').write(d)

main()
