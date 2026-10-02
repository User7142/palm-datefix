#
# DateFix - moves the Palm OS date window to 1932..2059
#
# make          -> build/DateFix.prc
# make test     -> host unit tests of the calendar arithmetic
#

# toolchain (palmdev-macos, prc-tools-remix; no palmdev-prep/sudo needed)
PALMDEV  ?= $(HOME)/tools/palmdev-macos
TOOLS    = $(PALMDEV)/toolchain
SDK      = $(PALMDEV)/sdk/sdk-4
PILRC    ?= $(HOME)/tools/pilrc-3.2-64bit/bin/pilrc
export GCC_EXEC_PREFIX = $(TOOLS)/lib/gcc-lib/

SDKINCS  := $(addprefix -I,$(shell find $(SDK)/include -type d))

CC68K    = $(TOOLS)/bin/m68k-palmos-gcc
CFLAGS68K = -O2 -Wall -nostdinc \
	    -B$(TOOLS)/lib/gcc-lib/m68k-palmos/2.95.3-kgpd/ \
	    -B$(TOOLS)/m68k-palmos/bin/ -B$(TOOLS)/m68k-palmos/lib/ \
	    -I$(TOOLS)/lib/gcc-lib/m68k-palmos/2.95.3-kgpd/include $(SDKINCS) \
	    -Ibuild -Isrc -L$(SDK)/lib $(DEFS)

CCARM    = $(TOOLS)/bin/arm-palmos-gcc
CFLAGSARM = -O2 -Wall -marm -mcpu=arm9tdmi -mthumb-interwork -ffixed-r9 \
	    -mno-apcs-frame -fomit-frame-pointer \
	    -fno-common -B$(TOOLS)/lib/gcc-lib/arm-palmos/3.3.1/ \
	    -B$(TOOLS)/arm-palmos/bin/ -Isrc $(DEFS)
OBJCOPYARM = $(TOOLS)/bin/arm-palmos-objcopy
NMARM    = $(TOOLS)/bin/arm-palmos-nm
BUILDPRC = $(TOOLS)/bin/build-prc

ARMOBJS  = build/datefix_arm.o build/calendar_arm.o

.PHONY: all test clean apps

all: build/DateFix.prc

build:
	mkdir -p build

build/%_arm.o: src/%_arm.c src/calendar.h src/types.h | build
	$(CCARM) $(CFLAGSARM) -c $< -o $@

build/calendar_arm.o: src/calendar.c src/calendar.h src/types.h | build
	$(CCARM) $(CFLAGSARM) -c $< -o $@

# The native code runs wherever the resource happens to be: link it at two
# addresses and insist on identical binaries (no absolute addresses).
build/datefix.armc: $(ARMOBJS)
	$(CCARM) $(CFLAGSARM) -nostdlib -nostartfiles -Wl,-e,DfInstall \
	  -Wl,-Ttext,0 $(ARMOBJS) -lgcc -o build/datefix_arm.elf
	$(CCARM) $(CFLAGSARM) -nostdlib -nostartfiles -Wl,-e,DfInstall \
	  -Wl,-Ttext,0x10000 $(ARMOBJS) -lgcc -o build/datefix_arm_moved.elf
	$(OBJCOPYARM) -O binary -j .text build/datefix_arm.elf $@
	$(OBJCOPYARM) -O binary -j .text build/datefix_arm_moved.elf build/datefix_moved.armc
	cmp $@ build/datefix_moved.armc
	python3 tools/check_sections.py build/datefix_arm.elf

# offsets of the native entry points for the 68k side
build/armc_offsets.h: build/datefix.armc tools/gen_offsets.py
	$(NMARM) build/datefix_arm.elf | python3 tools/gen_offsets.py > $@

build/datefix.o: src/datefix.c src/datefix.h src/convert.h build/armc_offsets.h
	$(CC68K) $(CFLAGS68K) -c $< -o $@

build/convert.o: src/convert.c src/convert.h
	$(CC68K) $(CFLAGS68K) -c $< -o $@

build/apppatch.o: src/apppatch.c src/apppatch.h src/apptable.h src/datefix.h
	$(CC68K) $(CFLAGS68K) -c $< -o $@

build/apptable.o: src/apptable.c src/apptable.h src/apppatch.h
	$(CC68K) $(CFLAGS68K) -c $< -o $@

# the application table (apps/apps.txt) as resource 'DFat' 1000; build-prc
# takes every build/*.bin by its name
build/DFat03e8.bin: apps/apps.txt tools/apptable.py | build
	python3 tools/apptable.py apps/apps.txt --resource $@

# the same table as a database, to update the table without a new DateFix
apps: build/DateFixApps.pdb
build/DateFixApps.pdb: apps/apps.txt tools/apptable.py | build
	python3 tools/apptable.py apps/apps.txt --pdb $@

build/selectday.o: src/selectday.c src/selectday.h src/datefix.h
	$(CC68K) $(CFLAGS68K) -c $< -o $@

build/clockcheck.o: src/clockcheck.c src/clockcheck.h src/calendar.h src/types.h
	$(CC68K) $(CFLAGS68K) -c $< -o $@

build/calendar_68k.o: src/calendar.c src/calendar.h src/types.h
	$(CC68K) $(CFLAGS68K) -c $< -o $@

build/m68k.o: src/m68k.c src/m68k.h src/calendar.h src/types.h
	$(CC68K) $(CFLAGS68K) -c $< -o $@

OBJS68K = build/datefix.o build/convert.o build/selectday.o build/clockcheck.o \
	  build/calendar_68k.o build/m68k.o build/apppatch.o build/apptable.o

build/datefix: $(OBJS68K) tools/check_reset_path.py
	$(CC68K) $(CFLAGS68K) $(OBJS68K) -o $@
	$(TOOLS)/bin/m68k-palmos-objdump -d $@ | python3 tools/check_reset_path.py

# The version shown in Options -> About comes from the VERSION file.
VERSION  := $(shell cat VERSION)

.PHONY: FORCE
build/version.rcp: FORCE | build
	@printf '%s\n' \
	  'VERSION ID 1 "$(VERSION)"' '' \
	  'ALERT ID aboutAlert' 'INFORMATION' 'BEGIN' \
	  '  TITLE "About DateFix"' \
	  '  MESSAGE "DateFix $(VERSION)\n\nMoves the Palm OS date limit from 2031 to a start year of your choice."' \
	  '  BUTTONS "OK"' 'END' > $@.tmp
	@cmp -s $@.tmp $@ && rm $@.tmp || mv $@.tmp $@

build/.resources: src/datefix.rcp src/datefix.h build/datefix.armc build/version.rcp $(wildcard src/icon/*.bmp) | build
	$(PILRC) -q -I src -I build src/datefix.rcp build
	touch $@

build/DateFix.prc: build/datefix build/.resources build/DFat03e8.bin
	$(BUILDPRC) -o $@ -t appl -c DtFx -n DateFix build/datefix build/*.bin

test: test-calendar test-convert test-apppatch test-apptable

.PHONY: test-calendar test-convert test-apppatch test-apptable
test-apppatch:
	/usr/bin/clang -O2 -Wall -DHOST_TEST -Isrc src/apppatch.c tests/apppatch_test.c -o tests/apppatch_test
	./tests/apppatch_test

test-apptable:
	python3 tools/apptable.py apps/apps.txt --resource tests/apptable.bin
	/usr/bin/clang -O2 -Wall -DHOST_TEST -Isrc src/apptable.c tests/apptable_test.c -o tests/apptable_test
	./tests/apptable_test tests/apptable.bin

test-convert:
	/usr/bin/clang -O2 -Wall -DHOST_TEST -Isrc src/convert.c tests/convert_test.c -o tests/convert_test
	./tests/convert_test

test-calendar:
	/usr/bin/clang -O2 -Wall -DHOST_TEST -Isrc src/calendar.c tests/dump.c -o tests/dump
	./tests/dump | python3 tests/check.py

clean:
	rm -rf build tests/dump tests/convert_test tests/apppatch_test tests/apptable_test tests/apptable.bin
