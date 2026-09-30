#!/usr/bin/env python3
"""
Checks the output of tests/dump against Python's datetime for every valid
start year: an internal day must give the internal date (real date minus
S - 1904 years, same month and day) and the weekday of the real date.
"""
import sys
from datetime import date, timedelta

E = date(1904, 1, 1)
errors = 0
counts = {}


def err(msg):
    global errors
    errors += 1
    if errors <= 20:
        print("FAIL", msg)


def palm_wd(d):
    return (d.weekday() + 1) % 7


for line in sys.stdin:
    f = line.split()
    kind = f[0]
    counts[kind] = counts.get(kind, 0) + 1
    if kind == "V":
        start, valid = int(f[1]), int(f[2])
        want = 1904 <= start <= 1972 and (start - 1904) % 4 == 0
        if bool(valid) != want:
            err(f"CalValidStartYear({start}) = {valid}")
    elif kind == "O":
        start, offset = int(f[1]), int(f[2])
        if offset != (date(start, 1, 1) - E).days:
            err(f"offset {start}: {offset}")
    elif kind == "S":
        start, d = int(f[1]), int(f[2])
        y, m, dd = map(int, f[3].split("-"))
        h, mi, se = map(int, f[4].split(":"))
        wd = int(f[5])
        internal = E + timedelta(days=d)
        real = date(internal.year + start - 1904, internal.month, internal.day)
        if (y, m, dd) != (internal.year, internal.month, internal.day) or (h, mi, se) != (12, 34, 56):
            err(f"S={start} day {d}: {y}-{m}-{dd} {h}:{mi}:{se}, want {internal}")
        if wd != palm_wd(real):
            err(f"S={start} weekday of {real}: {wd}")
        # the shift must be exact: internal day + offset = real day
        if (real - E).days != d + (date(start, 1, 1) - E).days:
            err(f"S={start}: shift not constant at {internal}")
    elif kind == "M":
        if f[2] != "2031-12-31" or f[3] != "23:59:59":
            err(f"clamp S={f[1]}: {f[2]} {f[3]}")
    elif kind == "W":
        start, y, m, wd, dom = map(int, f[1:])
        real_y = y + start - 1904
        if wd != palm_wd(date(real_y, m, 13)):
            err(f"DayOfWeek S={start} {m}/13/{y}")
        exp = ((28 - 1) // 7) * 7 + palm_wd(date(real_y, m, 28))
        if dom != exp:
            err(f"DayOfMonth S={start} {m}/28/{y}: {dom} != {exp}")

print(f"checked {counts}, {errors} errors")
sys.exit(1 if errors else 0)
