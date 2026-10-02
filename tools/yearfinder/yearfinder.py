#!/usr/bin/env python3
"""yearfinder - finds the places in a Palm OS application (.prc) that turn a DateType year into a real year.

Why: DateFix moves the 128-year window, so an application that shows `year + 1904` itself (instead of calling
DateToAscii & co.) shows the *internal* year. Such places need a per-application patch. This tool lists the
candidates, so a patch table entry (resource, offset, expected bytes, what to add) can be made by hand.

How: every `code` resource is disassembled (m68k-palmos-objdump); every instruction with the constant 1904
(firstYear, 0x0770) is a candidate. The next instructions are scanned for what the value is used for:
  DRAW   handed to StrIToA / StrPrintF  -> the year is shown, this is what needs a patch
  CALLDRAW  handed to one of the application's own functions that draws a number (follows 2 levels)\n  CALL   handed to an own function that does not draw (check by hand if in doubt)\n  API    handed to a date function (DayOfWeek, DateToDays, ...) -> right as it is, needs no patch
  STORE  written into a structure (DateTimeType.year ...) -> look at the trap that follows
  CMP    compared with 1904 (range check)
Only a scan, not a proof: check DRAW entries with Show Trace / on the device before patching.

usage: yearfinder.py app.prc [--window 14] [--json out.json] [--objdump PATH] [--all]
"""
import argparse, json, os, re, struct, subprocess, sys, tempfile

OBJDUMP = os.path.expanduser("~/tools/palmdev-macos/toolchain/bin/m68k-palmos-objdump")

TRAPS = {  # SDK Core/CoreTraps.h
    0xA0C5: "StrCopy", 0xA0C6: "StrCat", 0xA0C7: "StrLen", 0xA0C9: "StrIToA", 0xA0CC: "StrChr",
    0xA0F5: "TimGetSeconds", 0xA0FC: "TimSecondsToDateTime", 0xA0FD: "TimDateTimeToSeconds", 0xA0FE: "TimAdjust",
    0xA220: "WinDrawChars", 0xA25F: "DayOfWeek", 0xA260: "DaysInMonth", 0xA261: "DayOfMonth",
    0xA262: "DateDaysToDate", 0xA263: "DateToDays", 0xA264: "DateAdjust", 0xA265: "DateSecondsToDate",
    0xA266: "DateToAscii", 0xA267: "DateToDOWDMFormat", 0xA268: "TimeToAscii", 0xA2D0: "SelectDay",
    0xA2DE: "StrPrintF", 0xA2DF: "StrVPrintF", 0xA3CD: "DateTemplateToAscii", 0xA350: "WinDrawChar",
    0xA351: "WinDrawTruncChars", 0xA2CE: "StrNCopy",
}
DRAW = {"StrIToA", "StrPrintF", "StrVPrintF"}
API = {"DayOfWeek", "DaysInMonth", "DayOfMonth", "DateDaysToDate", "DateToDays", "DateAdjust", "DateSecondsToDate",
       "DateToAscii", "DateToDOWDMFormat", "DateTemplateToAscii", "TimSecondsToDateTime", "TimDateTimeToSeconds",
       "TimAdjust", "SelectDay"}


def resources(data):
    """(type, id, bytes) of every resource of a .prc."""
    n = struct.unpack(">H", data[76:78])[0]
    ents = [(data[78 + i * 10:82 + i * 10].decode("latin1"), struct.unpack(">H", data[82 + i * 10:84 + i * 10])[0],
             struct.unpack(">I", data[84 + i * 10:88 + i * 10])[0]) for i in range(n)]
    ends = [e[2] for e in ents[1:]] + [len(data)]
    return [(t, i, data[o:e]) for (t, i, o), e in zip(ents, ends)]


LINE = re.compile(r"^\s*([0-9a-f]+):\t((?:[0-9a-f]{4} ?)+)\s*\t?(.*)$")


def disassemble(code, objdump):
    with tempfile.NamedTemporaryFile(suffix=".bin") as f:
        f.write(code)
        f.flush()
        out = subprocess.run([objdump, "-D", "-b", "binary", "-m", "m68k", f.name], capture_output=True, text=True,
                             check=True).stdout
    ins = []
    for ln in out.splitlines():
        m = LINE.match(ln)
        if m:
            words = m.group(2).split()
            ins.append({"addr": int(m.group(1), 16), "hex": "".join(words), "text": m.group(3).strip()})
    return ins


def trap_at(ins, i):
    """Name of the A-line trap called by the `trap #15` at index i, else None."""
    if ins[i]["text"].startswith("trap #15") and i + 1 < len(ins):
        w = ins[i + 1]["hex"][:4]
        if w[0] in "aA":
            v = int(w, 16)
            return TRAPS.get(v, "trap 0x%04X" % v)
    return None


def call_target(ins, j):
    """Address of the function called by ins[j], or None. Understands `jsr/bsr pc@(X)` and the far-call idiom
    `pea ret; pea X; addil #N,%sp@; rts` of the segment loader (target = X + N)."""
    x = ins[j]["text"]
    m = re.match(r"[jb]sr[a-z]? (?:%pc@\()?(0x[0-9a-f]+)", x)
    if m:
        return int(m.group(1), 16)
    if x.startswith("addil #") and x.endswith(",%sp@") and j + 1 < len(ins) and ins[j + 1]["text"] == "rts":
        m2 = re.match(r"pea %pc@\((0x[0-9a-f]+)\)", ins[j - 1]["text"])
        if m2:
            return int(m2.group(1), 16) + int(x.split("#")[1].split(",")[0])
    return None


def draws_below(ins, index, addr, depth):
    """Does the function at addr (or something it calls, `depth` levels) hand a number to StrIToA/StrPrintF?"""
    if depth < 0 or addr not in index:
        return False
    i = index[addr]
    for j in range(i, min(i + 160, len(ins))):
        x = ins[j]["text"]
        if x.startswith(("unlk", "rte")) or (x == "rts" and not ins[j - 1]["text"].startswith("addil")):
            return False
        if trap_at(ins, j) in DRAW:
            return True
        t = call_target(ins, j)
        if t and draws_below(ins, index, t, depth - 1):
            return True
    return False


def analyse(ins, index, i, window):
    """Classify the 1904 constant at ins[i] by following the value in its register.

    The sum is tracked through register copies until it is pushed as an argument; the first call after that
    decides: a trap (StrIToA & co. = DRAW, date function = API), a call of the application's own function
    (CALLDRAW if that function draws a number, else CALL). A store into a structure is STORE, into a local
    variable LOCAL (shown later by a push of the local, to be followed by hand), a compare only CMP."""
    t = ins[i]["text"]
    if t.startswith("cmp"):
        return "CMP", [], [], []
    m = re.search(r"#1904,%(d\d)", t)
    if not m:
        return "?", [], [], []
    tracked, pushed = {m.group(1)}, False
    traps, stores, calls, seen = [], [], [], set()
    for j in range(i + 1, min(i + 1 + window, len(ins))):
        x = ins[j]["text"]
        if x.startswith(("unlk", "rte")) or (x == "rts" and not ins[j - 1]["text"].startswith("addil")):
            break
        tr = trap_at(ins, j)
        tgt = call_target(ins, j)
        if pushed and (tr or tgt):
            if tr:
                traps.append(tr)
                return ("DRAW" if tr in DRAW else "API" if tr in API else "OTHER"), stores, traps, calls
            calls.append(tgt)
            return ("CALLDRAW" if draws_below(ins, index, tgt, 2) else "CALL"), stores, traps, calls
        for r in list(tracked):
            if re.match(r"move[wl] %%%s,%%sp@-$" % r, x):
                pushed = True
            mc = re.match(r"move[wl] %%%s,%%(d\d)$" % r, x)
            if mc:
                tracked.add(mc.group(1))
            ms = re.match(r"move[wl] %%%s,%%a\d@\((-?\d+)\)$" % r, x)
            if ms:
                stores.append(ms.group(1))
                seen.add("STORE")
            ml = re.match(r"move[wl] %%%s,%%fp@\((-?\d+)\)$" % r, x)
            if ml:
                stores.append("fp" + ml.group(1))
                seen.add("LOCAL")
            if re.match(r"cmp\w* (?:.*,)?%%%s$|cmp\w* %%%s," % (r, r), x):
                seen.add("CMP")
        md = re.match(r"(?:moveq|move[bwl]|clr[bwl]|lea)\b.*,%(d\d)$", x)
        if md and md.group(1) in tracked and not re.match(r"move[wl] %%d\d,%%%s$" % md.group(1), x):
            if not re.search(r"%%%s\b" % md.group(1), x.split(",")[0]):
                tracked.discard(md.group(1))
                if not tracked and not pushed:
                    break
    for k in ("LOCAL", "STORE", "CMP"):
        if k in seen:
            return k, stores, traps, calls
    return "?", stores, traps, calls


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("prc")
    ap.add_argument("--window", type=int, default=24, help="instructions to scan after the constant")
    ap.add_argument("--json")
    ap.add_argument("--objdump", default=OBJDUMP)
    ap.add_argument("--all", action="store_true", help="also list API/STORE/CMP (default: DRAW, CALLDRAW, LOCAL, OTHER and ? only)")
    ap.add_argument("--table", nargs=2, metavar=("NAME", "CREATOR"),
                    help="print the DRAW and CALLDRAW sites as lines for apps/apps.txt "
                         "(only `addi.w #1904,Dn`; check every site in the application before adding it)")
    a = ap.parse_args()
    data = open(a.prc, "rb").read()
    found = []
    for typ, rid, body in resources(data):
        if typ != "code" or rid == 0 or len(body) < 4:
            continue
        ins = disassemble(body, a.objdump)
        index = {x["addr"]: n for n, x in enumerate(ins)}
        for i, x in enumerate(ins):
            if "#1904" not in x["text"] and "#1905" not in x["text"]:
                continue
            kind, stores, traps, calls = analyse(ins, index, i, a.window)
            ctx = "".join(y["hex"] for y in ins[max(0, i - 2):i + 3])
            found.append({"resource": "code %d" % rid, "res": rid, "size": len(body),
                          "offset": x["addr"], "insn": x["text"], "kind": kind,
                          "stores_to": stores, "traps_after": traps, "calls": ["0x%x" % c for c in calls], "context_hex": ctx})
    shown = [f for f in found if a.all or f["kind"] in ("DRAW", "CALLDRAW", "LOCAL", "OTHER", "?")]
    counts = {}
    for f in found:
        counts[f["kind"]] = counts.get(f["kind"], 0) + 1
    for f in shown:
        print("%-7s 0x%05x  %-6s %-28s -> %s" % (f["resource"], f["offset"], f["kind"], f["insn"],
                                                 ", ".join(f["traps_after"][:3] + f["calls"][:2]) or "-"))
    print("\n%d sites with 1904: %s" % (len(found), ", ".join("%s %d" % kv for kv in sorted(counts.items()))))
    if a.json:
        json.dump(found, open(a.json, "w"), indent=1)
    if a.table:
        # apps.txt "first-year" means addi.w #1904,Dn: other instructions need a
        # long site line with opcode and mask, written by hand
        lines = [f for f in found if f["kind"] in ("DRAW", "CALLDRAW")
                 and re.match(r"addiw#1904,%d[0-7]$", f["insn"].replace(" ", ""))]
        print("\n# %s (%s), from tools/yearfinder" % tuple(a.table))
        print("app %s %s" % tuple(a.table))
        for f in lines:
            print("site %d %d 0x%04x first-year    # %s" % (f["res"], f["size"], f["offset"], f["kind"]))


if __name__ == "__main__":
    sys.exit(main())
