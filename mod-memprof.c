/*
Copyright (C) 2013  
Baptiste Lepers < baptiste.lepers@gmail.com >

Many parts of the code copyied from the DProf profiler,
Copyright(C) 2010
Aleksey Pesterev < alekseyp@mit.edu >

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
version 2, as published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/moduleparam.h>
#include <linux/device.h>
#include <linux/pci.h>
#include <linux/kdebug.h>
#include <linux/kdebug.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/random.h>
#include <linux/utsname.h>
#include <linux/uaccess.h>
#include <linux/hardirq.h>
#include <asm/stacktrace.h>
#include <asm/nmi.h>
#include <asm/uaccess.h>
#include <linux/highmem.h>

#include "ibs/nmi_int.h"
#include "ibs/ibs.h"

#include "memprof-structs.h"
#include "mod-memprof.h"
#include "perf.h"

int max_cnt_op = 0x8FFF0;               // sampling period
int ibs_filter = IBS_INCLUDE_L3 
               | IBS_INCLUDE_REMOTE_CACHE 
               | IBS_INCLUDE_DRAM;
               //| IBS_INCLUDE_INVALID;  // Invalid = not touching memory or local L1 & L2 (majority of samples)
int ibs_remove_invalid_phys = 1;       // Remove samples not touching memory


DEFINE_PER_CPU(struct sample_buffer *, sample_buffers);

extern const struct file_operations memprof_perf_data_fops;
extern const struct file_operations memprof_raw_data_fops;
extern const struct file_operations memprof_cntrl_fops;

void set_ibs_rate(int cnt, int ops) {
   unsigned int low, high;
   uint32_t rand = 0;

   low = (((cnt + rand) >> 4) & 0xFFFF)
      + ((ops & 0x1) << 19) 
      + IBS_OP_LOW_ENABLE;
   high = 0;
   wrmsr(MSR_AMD64_IBSOPCTL, low, high);
}

static int nmi_handler(struct pt_regs * const regs)
{
   unsigned int low, high;
   int cpu = smp_processor_id();
   struct sample_buffer *sb;
   struct s *ibs_op;

   sb = per_cpu(sample_buffers, cpu);
   if (sb->count >= NUM_CPU_SAMPLES) {
      sb->overflow++;
      goto exit;
   }

   rdmsr(MSR_AMD64_IBSOPCTL, low, high);
   if (low & IBS_OP_LOW_VALID_BIT) {
      ibs_op = &sb->samples[sb->count];

      rdmsr(MSR_AMD64_IBSOPDATA2, low, high);
      ibs_op->ibs_op_data2_low = low;
      if(!(ibs_filter & (1 << (ibs_op->ibs_op_data2_low & 3))))
         goto end;

      rdmsr(MSR_AMD64_IBSDCPHYSAD, low, high);
      ibs_op->ibs_dc_phys = (((long long)high) << 32LL) + low;
      if(ibs_remove_invalid_phys && ibs_op->ibs_dc_phys == 0)
         goto end;

      sb->count++;

      rdtscll(ibs_op->rdt);
      ibs_op->cpu = cpu;

      rdmsr(MSR_AMD64_IBSOPRIP, low, high);
      ibs_op->rip = (((long long)high) << 32LL) + low;
      rdmsr(MSR_AMD64_IBSOPDATA, low, high);
      ibs_op->ibs_op_data1_low = low;
      ibs_op->ibs_op_data1_high = high;
      rdmsr(MSR_AMD64_IBSOPDATA3, low, high);
      ibs_op->ibs_op_data3_low = low;
      ibs_op->ibs_op_data3_high = high;
      rdmsr(MSR_AMD64_IBSDCLINAD, low, high);
      ibs_op->ibs_dc_linear = (((long long)high) << 32LL) + low;
      
		ibs_op->tid = current->pid;
		ibs_op->pid = current->tgid;
		ibs_op->kern = !user_mode(regs);
		memcpy(ibs_op->comm, current->comm, TASK_COMM_LEN);

      ibs_op->usersp = current->thread.usersp;
      ibs_op->stack = (uint64_t)current->stack;

      /* reenable the IRQ */
end:
      rdmsr(MSR_AMD64_IBSOPCTL, low, high);
      high = 0;
      low &= ~IBS_OP_LOW_VALID_BIT;
      low |= IBS_OP_LOW_ENABLE;
      wrmsr(MSR_AMD64_IBSOPCTL, low, high);
   }

exit:
   return 1; // report success
}

static void memprof_start(void) {
   set_ibs_rate(max_cnt_op, 0);
}

static void memprof_stop(void) {
   unsigned int low, high;
   low = 0;		// clear max count and enable
   high = 0;
   wrmsr(MSR_AMD64_IBSOPCTL, low, high);
}

static void memprof_shutdown(void) {
   on_each_cpu(apic_clear_ibs_nmi_per_cpu, NULL, 1);
}

static void memprof_setup(void) {
}

static struct memprof_model model = {
   .setup = memprof_setup,
   .shutdown = memprof_shutdown,
   .start = memprof_start,
   .stop = memprof_stop,
   .check_ctrs = nmi_handler,
};

static void free_cpu_buffers(void) {
   int cpu;

   for_each_possible_cpu(cpu) {
      struct sample_buffer *sb;
      sb = per_cpu(sample_buffers, cpu);
      vfree(sb);
      per_cpu(sample_buffers, cpu) = NULL;
   }

   free_perf_buffers();
}

static int alloc_cpu_buffers(void) {
   int cpu;

   for_each_possible_cpu(cpu) {
      struct sample_buffer *sb;
      sb = vmalloc_node(sizeof(struct sample_buffer), cpu_to_node(cpu));
      if (!sb)
         goto fail;
      per_cpu(sample_buffers, cpu) = sb;
      sb->count = 0;
   }

   return alloc_perf_buffers();
fail:
   free_cpu_buffers();
   return -ENOMEM;
}


static int __init memprof_init(void) {
   int err;

   if (!boot_cpu_has(X86_FEATURE_IBS)) {
      printk(KERN_ERR "AMD IBS not present in hardware\n");
      return -ENODEV;
   }

   err = memprof_nmi_init(&model);
   if (err)
      return err;

   err = alloc_cpu_buffers();
   if (err)
      return err;

   ibs_nmi_setup();
   pfm_amd64_setup_eilvt();

   proc_create("memprof_ibs", S_IRUGO, NULL, &memprof_raw_data_fops);
   proc_create("memprof_cntl", S_IWUGO, NULL, &memprof_cntrl_fops);
   proc_create("memprof_perf", S_IRUGO, NULL, &memprof_perf_data_fops);
   
   init_hooks();
   return 0;
}

static void __exit memprof_exit(void) {
   memprof_nmi_shutdown();
   memprof_nmi_exit();

   remove_proc_entry("memprof_ibs", NULL);
   remove_proc_entry("memprof_perf", NULL);
   remove_proc_entry("memprof_cntl", NULL);

   clear_hooks();
   free_cpu_buffers();
}

module_init(memprof_init);
module_exit(memprof_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Baptiste Lepers <baptiste.lepers@gmail.com>");
MODULE_DESCRIPTION("Kernel IBS Module");
