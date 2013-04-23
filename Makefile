obj-m += memprof.o
memprof-objs := ibs/nmi_int.o mod-memprof.o perf.o proc.o hijack.o
KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

.PHONY: hooks.h

default: hooks.h
	        $(MAKE) -C $(KDIR) SUBDIRS=$(PWD) modules

hooks.h: create_hooks.pl
	   $(PWD)/create_hooks.pl $(PWD)

clean:
	        $(MAKE) -C $(KDIR) SUBDIRS=$(PWD) clean
		rm -f modules.order
