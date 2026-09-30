#!/usr/bin/env python3
"""
Fails if the native code has anything besides .text: data, bss, GOT or
read-only data would need addresses the resource does not have.
"""
import subprocess
import sys
import os

objdump = os.path.join(os.environ.get("GCC_EXEC_PREFIX", ""), "..", "..", "bin",
                       "arm-palmos-objdump")
out = subprocess.run([objdump, "-h", sys.argv[1]], capture_output=True,
                     text=True, check=True).stdout

bad = []
for line in out.splitlines():
    fields = line.split()
    if len(fields) >= 3 and fields[0].isdigit():
        name, size = fields[1], int(fields[2], 16)
        if name != ".text" and size != 0 and not name.startswith((".comment", ".debug", ".ARM.attributes")):
            bad.append(f"{name} ({size} bytes)")

if bad:
    print("native code must be .text only, found: " + ", ".join(bad))
    sys.exit(1)
