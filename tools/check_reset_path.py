#!/usr/bin/env python3
"""
Fails if code that runs on sysAppLaunchCmdSystemReset touches globals.

An application gets no globals on that launch code; register A5 then points
nowhere and any access to global data (prc-tools keeps initialised data,
even const tables, in the globals) crashes the device while it boots.

Reads `m68k-palmos-objdump -d` output on stdin and checks every function
reachable from the reset path for A5-relative accesses.
"""
import re
import sys

# PilotMain runs in both launches; its normal-launch branch only calls the
# UI (EventLoop etc.), which is not followed here. DfSelectDay runs inside
# other applications (installed over the SelectDay trap): their A5.
ROOTS = ["PilotMain", "DfSelectDay", "M68kSecondsToDateTime", "M68kDayOfWeek",
         "M68kDayOfMonth", "M68kDateToAscii", "M68kDateToDOWDMFormat",
         "M68kDateTemplateToAscii"]
UI_ONLY = {"EventLoop", "MainFormHandleEvent", "MainFormUpdate", "TableFormDraw",
           "TableFormHandleEvent", "SelfTest", "Uninstall", "SetEnabled",
           "Enable", "Disable", "ReapplyAppPatches"}

funcs, current = {}, None
for line in sys.stdin:
    m = re.match(r"^[0-9a-f]+ <([A-Za-z_]\w*)>:", line)
    if m:
        current = m.group(1)
        funcs[current] = []
    elif current:
        funcs[current].append(line.rstrip())

seen, todo, bad = set(), list(ROOTS), []
while todo:
    name = todo.pop()
    if name in seen or name in UI_ONLY or name not in funcs:
        continue
    seen.add(name)
    for line in funcs[name]:
        if re.search(r"%a5@\(", line):
            bad.append(f"{name}: {line.strip()}")
        for callee in re.findall(r"\b(?:jsr|bsr\w*)\s+[0-9a-f]+ <(\w+)>", line):
            todo.append(callee)

if bad:
    print("globals used on the reset path:\n  " + "\n  ".join(bad))
    sys.exit(1)
print(f"reset path free of globals ({len(seen)} functions)")
