Memprof Kernel Module
=====================
The Memprof Kernel Module collects IBS samples and sets up hooks in the perf event subsystem.

WARNING
=======

This module only works on AMD processors. Intel processors are not yet supported by Memprof.

How to compile
==============
This kernel module overloads some kernel functions that are not directly exported to modules. In order to do that, the module looks for symbols in /boot/System.map-your-kernel-version.

Most distributions are shipped with a System.map file. If you do not have a System.map file, you can obtain one by downloading a new kernel and compiling it as follows:

```bash
make -j64

KN="vmlinuz"
KV=`make kernelversion`
sudo make modules_install;
sudo cp arch/x86/boot/bzImage /boot/${KN}-${KV}
sudo cp System.map /boot/System.map-${KV}
sudo cp .config /boot/config-${KV}
sudo mkinitramfs -o /boot/initrd.img-${KV} ${KV}
```


WARNING: this module has only been tested on kernels 3.6 to 3.8.


Usage
=====

```bash
../scripts/profile_app.sh <app>

or 

sudo insmod ./memprof.ko 
rm /tmp/data.*
echo b > /proc/memprof_cntl 
LD_PRELOAD=../library/ldlib.so <app> 
echo e > /proc/memprof_cntl 
cat /proc/memprof_ibs > ibs.raw 
cat /proc/memprof_perf > perf.raw 
../library/merge /tmp/data.* 
../parser/parse ibs.raw --data data.processed.raw --perf perf.raw [options, e.g. -M] 
```


Notes
=====
To set sampling frequency change the mod-memprof.c code:
int max_cnt_op = your-value;

Reference
=========
Memprof is described in the following paper "MemProf: A Memory Proﬁler for NUMA Multicore Systems".
