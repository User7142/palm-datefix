# diagnostic probes for the fix log: make -C tools/probes
PALMDEV ?= $(HOME)/tools/palmdev-macos
TOOLS    = $(PALMDEV)/toolchain
SDK      = $(PALMDEV)/sdk/sdk-4
export GCC_EXEC_PREFIX = $(TOOLS)/lib/gcc-lib/
SDKINCS := $(addprefix -I,$(shell find $(SDK)/include -type d))
CC68K    = $(TOOLS)/bin/m68k-palmos-gcc -O2 -Wall -nostdinc \
	   -B$(TOOLS)/lib/gcc-lib/m68k-palmos/2.95.3-kgpd/ -B$(TOOLS)/m68k-palmos/bin/ \
	   -B$(TOOLS)/m68k-palmos/lib/ -I$(TOOLS)/lib/gcc-lib/m68k-palmos/2.95.3-kgpd/include \
	   $(SDKINCS) -L$(SDK)/lib
CCARM    = $(TOOLS)/bin/arm-palmos-gcc -O2 -marm -mno-apcs-frame -fomit-frame-pointer \
	   -ffixed-r9 -nostdlib -nostartfiles -B$(TOOLS)/lib/gcc-lib/arm-palmos/3.3.1/ \
	   -B$(TOOLS)/arm-palmos/bin/
all: ExportProbe.prc FormProbe.prc YearProbe.prc TimeCopyFeed.prc
ExportProbe.prc: exportprobe.c exportprobe_arm.c
	mkdir -p out
	$(CCARM) -Wl,-e,Read -Wl,-Ttext,0 exportprobe_arm.c -o out/arm.elf
	$(TOOLS)/bin/arm-palmos-objcopy -O binary -j .text out/arm.elf out/armc03e8.bin
	$(CC68K) exportprobe.c -o out/exportprobe
	printf 'APPLICATIONICONNAME ID 1000 "ExportProbe"\n' > out/probe.rcp
	$(HOME)/tools/pilrc-3.2-64bit/bin/pilrc -q out/probe.rcp out
	$(TOOLS)/bin/build-prc -o $@ -t appl -c ExPb -n ExportProbe out/exportprobe out/*.bin
clean:
	rm -rf out ExportProbe.prc FormProbe.prc YearProbe.prc TimeCopyFeed.prc

FormProbe.prc: formprobe.c
	mkdir -p out/form
	$(CC68K) formprobe.c -o out/form/formprobe
	printf 'APPLICATIONICONNAME ID 1000 "FormProbe"\n' > out/form/probe.rcp
	$(HOME)/tools/pilrc-3.2-64bit/bin/pilrc -q out/form/probe.rcp out/form
	$(TOOLS)/bin/build-prc -o $@ -t appl -c FmPb -n FormProbe out/form/formprobe out/form/*.bin

YearProbe.prc: yearprobe.c
	mkdir -p out/year
	$(CC68K) yearprobe.c -o out/year/yearprobe
	printf 'APPLICATIONICONNAME ID 1000 "YearProbe"\n' > out/year/probe.rcp
	$(HOME)/tools/pilrc-3.2-64bit/bin/pilrc -q out/year/probe.rcp out/year
	$(TOOLS)/bin/build-prc -o $@ -t appl -c YrPb -n YearProbe out/year/yearprobe out/year/*.bin

TimeCopyFeed.prc: timecopyfeed.c
	mkdir -p out/tcfeed
	$(CC68K) timecopyfeed.c -o out/tcfeed/timecopyfeed
	printf 'APPLICATIONICONNAME ID 1000 "TcFeed"\n' > out/tcfeed/probe.rcp
	$(HOME)/tools/pilrc-3.2-64bit/bin/pilrc -q out/tcfeed/probe.rcp out/tcfeed
	$(TOOLS)/bin/build-prc -o $@ -t appl -c TcFd -n TimeCopyFeed out/tcfeed/timecopyfeed out/tcfeed/*.bin
