/*
Copyright (C) 2013  
Baptiste Lepers < baptiste.lepers@gmail.com >

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

#include "memprof-structs.h"
#include "hijack.h"
#include "hooks.h"
#include "perf.h"
#include "console.h"

DEFINE_PER_CPU(struct mmap_buffer*, mmap_buffers);
DEFINE_PER_CPU(struct task_buffer*, task_buffers);
DEFINE_PER_CPU(struct comm_buffer*, comm_buffers);

void free_perf_buffers(void) {
   int cpu;
   struct mmap_buffer *m;
   struct comm_buffer *c;
   struct task_buffer *t;

   for_each_online_cpu(cpu) {
      m = per_cpu(mmap_buffers, cpu);
      if(m)
         vfree(m);
      per_cpu(mmap_buffers, cpu) = NULL;

      c = per_cpu(comm_buffers, cpu);
      if(c)
         vfree(c);
      per_cpu(comm_buffers, cpu) = NULL;

      t = per_cpu(task_buffers, cpu);
      if(t)
         vfree(t);
      per_cpu(task_buffers, cpu) = NULL;
   }
}

int alloc_perf_buffers(void) {
   int cpu;
   struct mmap_buffer *m;
   struct comm_buffer *c;
   struct task_buffer *t;

   for_each_online_cpu(cpu) {
      m = vmalloc_node(sizeof(struct mmap_buffer), cpu_to_node(cpu));
      if (!m)
         goto fail;
      per_cpu(mmap_buffers, cpu) = m;
      m->count = 0;

      c = vmalloc_node(sizeof(struct comm_buffer), cpu_to_node(cpu));
      if (!c)
         goto fail;
      per_cpu(comm_buffers, cpu) = c;
      c->count = 0;

      t = vmalloc_node(sizeof(struct task_buffer), cpu_to_node(cpu));
      if (!t)
         goto fail;
      per_cpu(task_buffers, cpu) = t;
      t->count = 0;
   }
   return 0;

fail:
   free_perf_buffers();
   return -ENOMEM;
}

static void memprof_task_hook(struct task_struct *task, void *unused, int new) {
   int cpu = smp_processor_id();
   struct task_event *t;
   struct task_buffer *b = per_cpu(task_buffers, cpu);
   if(b->count >= NUM_MMAP_SAMPLES) {
      b->overflow++;
      return;
   }

   t = &b->samples[b->count];
   b->count++;

   rdtscll(t->rdt);
   t->pid = task->tgid;
   t->tid = task->pid;
   t->ppid = current->tgid;
   t->ptid = current->pid;
   t->new = new;
}
static asmlinkage void memprof_comm_hook(struct task_struct *task) {
   int cpu = smp_processor_id();
   struct comm_event *t;
   struct comm_buffer *b = per_cpu(comm_buffers, cpu);
   if(b->count >= NUM_MMAP_SAMPLES) {
      b->overflow++;
      return;
   }

   t = &b->samples[b->count];
   b->count++;

   rdtscll(t->rdt);
   t->pid = current->tgid;
   t->tid = current->pid;
   strncpy(t->comm, task->comm, sizeof(t->comm));
}

static const char *sdb_arch_vma_name(struct vm_area_struct *vma) {
   if (vma->vm_mm && vma->vm_start == (long)vma->vm_mm->context.vdso)
      return "[vdso]";
   return NULL;
}

static void perf_event_mmap_event(struct mmap_event *mmap_event, struct vm_area_struct *vma) {
   struct file *file = vma->vm_file;
   unsigned int size;
   char tmp[16];
   char *buf = NULL;
   const char *name;
   memset(tmp, 0, sizeof(tmp));

   if (file) {
      buf = kzalloc(PATH_MAX + sizeof(u64), GFP_KERNEL);
      if (!buf) {
         name = strncpy(tmp, "//enomem", sizeof(tmp));
         goto got_name;
      }
      name = d_path(&file->f_path, buf, PATH_MAX);
      if (IS_ERR(name)) {
         name = strncpy(tmp, "//toolong", sizeof(tmp));
         goto got_name;
      }
   } else {
      if (sdb_arch_vma_name(vma)) {
         name = strncpy(tmp, sdb_arch_vma_name(vma),
               sizeof(tmp));
         goto got_name;
      }

      if (!vma->vm_mm) {
         name = strncpy(tmp, "[vdso]", sizeof(tmp));
         goto got_name;
      } else if (vma->vm_start <= vma->vm_mm->start_brk &&
            vma->vm_end >= vma->vm_mm->brk) {
         name = strncpy(tmp, "[heap]", sizeof(tmp));
         goto got_name;
      } else if (vma->vm_start <= vma->vm_mm->start_stack &&
            vma->vm_end >= vma->vm_mm->start_stack) {
         name = strncpy(tmp, "[stack]", sizeof(tmp));
         goto got_name;
      }

      name = strncpy(tmp, "//anon", sizeof(tmp)); /* Anon is normal mmaped/malloc'ed memory */
      goto got_name;
   }

got_name:
   size = ALIGN(strlen(name)+1, sizeof(u64));
   strncpy(mmap_event->file_name, name, sizeof(mmap_event->file_name));
   mmap_event->file_size = size;
   kfree(buf);
}
static asmlinkage void memprof_mmap_hook(struct vm_area_struct *vma) {
   int cpu = smp_processor_id();
   struct mmap_event *m;
   struct mmap_buffer *b = per_cpu(mmap_buffers, cpu);
   if(b->count >= NUM_MMAP_SAMPLES) {
      b->overflow++;
      return;
   }

   m = &b->samples[b->count];
   b->count++;

   rdtscll(m->rdt);
   m->start  = vma->vm_start;
   m->len    = vma->vm_end - vma->vm_start;
   m->pgoff  = (u64)vma->vm_pgoff << PAGE_SHIFT;
   m->pid = current->tgid;
   m->tid = current->pid;
   perf_event_mmap_event(m, vma);
}

static asmlinkage void memprof_fork_hook(struct task_struct *task) {
   memprof_task_hook(task, NULL, 1);
}
static asmlinkage void memprof_exit_hook(struct task_struct *child) {
   memprof_task_hook(child, NULL, 0);
}

static struct hijack hooks[4];
void init_hooks(void) {
   intercept_init(&hooks[0], perf_event_mmap_hook, memprof_mmap_hook);
   intercept_init(&hooks[1], perf_event_comm_hook, memprof_comm_hook);
   intercept_init(&hooks[2], perf_event_fork_hook, memprof_fork_hook);
   intercept_init(&hooks[3], perf_event_exit_task_hook, memprof_exit_hook);
}

void set_hooks(void) {
   intercept_start(&hooks[0]);
   intercept_start(&hooks[1]);
   intercept_start(&hooks[2]);
   intercept_start(&hooks[3]);
   add_fake_perf_events();
}

void clear_hooks(void) {
   intercept_stop(&hooks[0]);
   intercept_stop(&hooks[1]);
   intercept_stop(&hooks[2]);
   intercept_stop(&hooks[3]);
}

void add_fake_perf_events(void) {
   struct task_struct *task;
   struct vm_area_struct *vma;
   struct mm_struct *mm;

   read_lock(tasklist_lock_hook);
   for_each_process(task)
   {
      // should never be true but we really want to have only 1 sample per pid (vs tid)
      if(task->pid != task->tgid)
         continue; 
      perf_event_fork_hook(task);
      perf_event_comm_hook(task);
      mm = task->mm;
      if(mm) {
         down_read(&mm->mmap_sem);
         for(vma = mm->mmap; vma; vma = vma->vm_next) {
            perf_event_mmap_hook(vma);
         }
         up_read(&mm->mmap_sem);
      }
   }
   read_unlock(tasklist_lock_hook);
}
