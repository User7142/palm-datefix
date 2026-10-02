#!/usr/bin/env python3
"""
Turns apps/apps.txt into DateFix's application table.

  apptable.py apps/apps.txt --resource build/DFat03e8.bin   # built into DateFix.prc
  apptable.py apps/apps.txt --pdb build/DateFixApps.pdb     # update without a new DateFix
  apptable.py apps/apps.txt --list                          # what the table contains

Binary layout (big-endian, read by src/apptable.c):

  0   'DFAT'                 magic
  4   u16 format             1
  6   u32 version            YYYYMMDDnn from the "table" line
  10  u16 apps
  12  per application:
        u32 creator
        char name[32]        database name, NUL-padded
        u16 sites
        per site (16 bytes): u16 resource, u32 size, u16 offset, u16 opcode,
                             u16 mask, u16 constant, i16 direction
"""
import argparse
import struct
import sys
import time

MAGIC = b"DFAT"
FORMAT = 1
NAME_LEN = 32
RESOURCE_TYPE = b"DFat"          # in DateFix.prc, id 1000
PDB_NAME = b"DateFixApps"
PDB_TYPE = b"DFat"
PDB_CREATOR = b"DtFx"            # DateFix's own creator: deleted together with it
FIRST_YEAR = (0x0640, 0xFFF8, 1904, 1)   # addi.w #1904,Dn


class TableError(Exception):
    pass


def parse(path):
    version, apps = None, []
    for no, raw in enumerate(open(path, encoding="ascii"), 1):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        f = line.split()
        where = "%s:%d" % (path, no)
        try:
            if f[0] == "table" and len(f) == 2:
                if version is not None:
                    raise TableError("second table line")
                if not (f[1].isdigit() and len(f[1]) == 10):
                    raise TableError("version must be YYYYMMDDnn")
                version = int(f[1])
            elif f[0] in ("app", "alias") and len(f) == 3:
                name, creator = f[1], f[2]
                if not 1 <= len(name) < NAME_LEN:
                    raise TableError("name must have 1..%d characters" % (NAME_LEN - 1))
                if len(creator) != 4:
                    raise TableError("creator must have 4 characters")
                if f[0] == "alias":
                    if not apps:
                        raise TableError("alias without an app above it")
                    sites = apps[-1]["sites"]
                else:
                    sites = []
                apps.append({"name": name, "creator": creator, "sites": sites, "alias": f[0] == "alias"})
            elif f[0] == "site" and len(f) in (5, 8):
                if not apps or apps[-1]["alias"]:
                    raise TableError("site must follow an app line")
                res, size, offset = int(f[1]), int(f[2]), int(f[3], 16)
                if len(f) == 5:
                    if f[4] != "first-year":
                        raise TableError("short site needs 'first-year'")
                    opcode, mask, constant, direction = FIRST_YEAR
                else:
                    opcode, mask, constant = int(f[4], 16), int(f[5], 16), int(f[6])
                    if f[7] not in ("+1", "-1"):
                        raise TableError("direction must be +1 or -1")
                    direction = int(f[7])
                if not (0 <= res < 0x10000 and 0 < size < 0x10000 * 64 and 0 <= offset < 0x10000):
                    raise TableError("resource, size or offset out of range")
                if offset % 2:
                    raise TableError("offset must be even (68k instructions are word-aligned)")
                if offset + 4 > size:
                    raise TableError("site lies beyond the end of the resource")
                if opcode & ~mask & 0xFFFF:
                    raise TableError("opcode has bits outside the mask")
                if not 1900 <= constant <= 2100:
                    raise TableError("constant is not a year")
                apps[-1]["sites"].append((res, size, offset, opcode, mask, constant, direction))
            else:
                raise TableError("unknown line")
        except (TableError, ValueError) as e:
            raise TableError("%s: %s: %s" % (where, e, raw.strip()))
    if version is None:
        raise TableError("%s: no 'table <version>' line" % path)
    creators = [a["creator"] for a in apps]
    if len(set(creators)) != len(creators):
        raise TableError("%s: a creator appears twice" % path)
    for a in apps:
        if not a["sites"]:
            raise TableError("%s: %s has no sites" % (path, a["name"]))
    return version, apps


def build(version, apps):
    out = bytearray(MAGIC + struct.pack(">HIH", FORMAT, version, len(apps)))
    for a in apps:
        out += a["creator"].encode("ascii")
        out += a["name"].encode("ascii").ljust(NAME_LEN, b"\0")
        out += struct.pack(">H", len(a["sites"]))
        for s in a["sites"]:
            out += struct.pack(">HIHHHHh", *s)
    return bytes(out)


def pdb(blob):
    """A record database with the table as its only record."""
    now = int(time.time()) + 2082844800          # Palm OS counts from 1904
    # name, attributes (backup bit), version, created, modified, backed up,
    # modification number, appInfo, sortInfo, type, creator, unique id seed,
    # next record list, number of records: 78 bytes
    header = struct.pack(">32sHHIIIIII4s4sIIH",
                         PDB_NAME.ljust(32, b"\0"), 0x0008, 1,
                         now, now, 0, 0, 0, 0,
                         PDB_TYPE, PDB_CREATOR, 0, 0, 1)
    assert len(header) == 78
    data_offset = 78 + 8 + 2
    record = struct.pack(">IB3s", data_offset, 0, b"\x00\x00\x01")
    return header + record + b"\0\0" + blob


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("table")
    ap.add_argument("--resource", help="write the table resource for build-prc")
    ap.add_argument("--pdb", help="write DateFixApps.pdb")
    ap.add_argument("--list", action="store_true")
    a = ap.parse_args()
    try:
        version, apps = parse(a.table)
    except TableError as e:
        print("apptable: %s" % e, file=sys.stderr)
        return 1
    blob = build(version, apps)
    if a.resource:
        open(a.resource, "wb").write(blob)
    if a.pdb:
        open(a.pdb, "wb").write(pdb(blob))
    if a.list or not (a.resource or a.pdb):
        print("table %d, %d applications, %d bytes" % (version, len(apps), len(blob)))
        for x in apps:
            print("  %-12s %s  %d site(s)%s" % (x["name"], x["creator"], len(x["sites"]),
                                              "  (alias)" if x["alias"] else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
