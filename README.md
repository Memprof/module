Memprof Kernel Module
=====================

The Memprof Kernel Module collects IBS samples and set up hooks in the perf event subsystem.

Usage:

```bash
make 
sudo insmod ./memprof.ko 
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
int max_cnt_op = 100000;

