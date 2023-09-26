# Compile njmon and nimon for Linux
CFLAGS=-g -O4 
LDFLAGS=-g -lm

VERSION=81
FILE=njmon_linux_v$(VERSION).c

HW=$(shell uname -p )

OSNAME=$(shell grep ^NAME /etc/os-release | sed 's/NAME=//' | sed 's/Red Hat Enterprise Linux Server/RHEL/' | sed 's/Red Hat Enterprise Linux Workstation/RHEL/' | sed 's/Red Hat Enterprise Linux/RHEL/' | sed 's/"//g' )

OSVERSION=$(shell grep ^VERSION_ID /etc/os-release | tr '"' '.' | cut --delimiter=. --fields=2 )

BINARY=$(OSNAME)$(OSVERSION)_$(HW)_v$(VERSION)
GPU=$(OSNAME)$(OSVERSION)_$(HW)_gpu_v$(VERSION)

binary: 
	cc $(FILE) -o njmon_$(BINARY) $(CFLAGS) $(LDFLAGS) -D OSNAME=\"$(OSNAME)\" -D OSVERSION=\"$(OSVERSION)\" -D HW=\"$(HW)\" 

Z: 
	cc $(FILE) -o njmon_$(BINARY) $(CFLAGS) $(LDFLAGS) -D OSNAME=\"$(OSNAME)\" -D OSVERSION=\"$(OSVERSION)\" -D HW=\"$(HW)\" -D MAINFRAME

list:
	@echo HW $(HW)
	@echo osname $(OSNAME)
	@echo osversion $(OSVERSION)
gpu: 
	cc $(FILE) -D NVIDIA_GPU -o njmon_$(GPU) $(CFLAGS) $(LDFLAGS) /usr/lib64/libnvidia-ml.so.1 -D OSNAME=\"$(OSNAME)\" -D OSVERSION=\"$(OSVERSION)\" -D HW=\"$(HW)\" 

clean:
	rm -f njmon nimon  njmon_gpu njmon_gpu

