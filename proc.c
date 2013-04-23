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
#include "ibs/nmi_int.h"
#include "memprof-structs.h"
#include "mod-memprof.h"
#include "perf.h"

/*
 *  /proc/memprof_cntl
 */
static int running;
static ssize_t memprof_proc_write(struct file *file, const char __user *buf,
      size_t count, loff_t *ppos) {
   char c;

   if (count) {
      if (get_user(c, buf))
         return -EFAULT;
      if (c == 'b' && !running) {
         memprof_nmi_start();
         running = 1;
      } else if(c == 'p') {
         set_hooks();
      } else if (c == 'e' && running) {
         memprof_nmi_stop();
         running = 0;
         clear_hooks();
      }
   }
   return count;
}

const struct file_operations memprof_cntrl_fops = {
   .write          = memprof_proc_write,
};

/*
 *  /proc/memprof_perf
 */
struct memprof_perf_iter {
   int step;
   int cpu;
   int i;
   int count;
   int cpu_count;
};

static int memprof_perf_count(int step, int cpu) {
   if(step == 0) {
      return per_cpu(mmap_buffers, cpu)->count;
   } else if(step == 1) {
      return per_cpu(comm_buffers, cpu)->count;
   } else if(step == 2) {
      return per_cpu(task_buffers, cpu)->count;
   } else {
      return 0;
   }
}

static int memprof_write_var(struct seq_file *f, int step, int cpu, int i) {
   if(step == 0) {
      struct mmap_event *m;
      struct mmap_buffer *b = per_cpu(mmap_buffers, cpu);
      if(i >= b->count) {
         return -1;
      }
      m = &b->samples[i];
      if(seq_write(f, m, sizeof(*m))) {
         return -1;
      }
   } else if(step == 1) {
      struct comm_event *m;
      struct comm_buffer *b = per_cpu(comm_buffers, cpu);
      if(i >= b->count) {
         return -1;
      }
      m = &b->samples[i];
      if(seq_write(f, m, sizeof(*m))) {
         return -1;
      }
   } else if(step == 2) {
      struct task_event *m;
      struct task_buffer *b = per_cpu(task_buffers, cpu);
      if(i >= b->count) {
         return -1;
      }
      m = &b->samples[i];
      if(seq_write(f, m, sizeof(*m))) {
         return -11;
      }
   } else {
      return -1;
   }
   return 0;
}

static int first_memprof_per_show;
static int memprof_perf_show(struct seq_file *m, void *v) {
   struct memprof_perf_iter *iter = (struct memprof_perf_iter *)v;
   if (!iter) 
      return 0;

   if(first_memprof_per_show) {
      int r;
      int cpu;
      struct m mm = {
         .nb_mmap_events = 0,
         .nb_comm_events = 0,
         .nb_task_events = 0,
      };

      struct h h = {
         .version = M_VERSION,
      };
      r = seq_write(m, &h, sizeof(h));
      if(r)
         printk("Error writing first header\n");

      for_each_online_cpu(cpu) {
         mm.nb_mmap_events += per_cpu(mmap_buffers, cpu)->count;
         mm.nb_comm_events += per_cpu(comm_buffers, cpu)->count;
         mm.nb_task_events += per_cpu(task_buffers, cpu)->count;
      }
      r = seq_write(m, &mm, sizeof(mm));
      if(r)
         printk("Error writing second header\n");
      first_memprof_per_show = 0;
   }
   return memprof_write_var(m, iter->step, iter->cpu, iter->i);
}

static inline int perf_valid(struct memprof_perf_iter *iter) {
   if (iter->cpu < iter->cpu_count && iter->step <= 2 && iter->i < memprof_perf_count(iter->step, iter->cpu)) 
      return 1;
   else
      return 0;
}

static void *memprof_perf_start(struct seq_file *p, loff_t *pos) {
   loff_t vpos = *pos;
   struct memprof_perf_iter *iter = kmalloc(sizeof(struct memprof_perf_iter), GFP_KERNEL);
   if (!iter)
      return ERR_PTR(-ENOMEM);

   iter->step = 0;
   iter->cpu = 0;
   iter->cpu_count = num_possible_cpus();
   iter->i = 0;
   iter->count = memprof_perf_count(iter->step, iter->cpu);
   if(vpos == 0)
      first_memprof_per_show = 1;

   while (true) {
      if (memprof_perf_count(iter->step, iter->cpu) <= vpos) {
         vpos -= memprof_perf_count(iter->step, iter->cpu);
         iter->cpu++;
         if(iter->cpu >= iter->cpu_count) {
            if(iter->step == 2) {
               return NULL;
            } else {
               iter->step++;
               iter->cpu = 0;
            }
         }
         iter->i = 0;
         iter->count = memprof_perf_count(iter->step, iter->cpu);
      } else {
         iter->i = vpos;
         if(!perf_valid(iter))
            return NULL;
         else
            return iter;
      }
   }
}

static void *memprof_perf_next(struct seq_file *p, void *v, loff_t *pos) {
   struct memprof_perf_iter *iter = (struct memprof_perf_iter *)v;
   do {
      iter->i++;
      if (iter->i >= memprof_perf_count(iter->step, iter->cpu)) {
         iter->i = 0;
         iter->cpu++;
         if (iter->cpu >= iter->cpu_count) {
            if(iter->step == 2) {
               kfree(iter);
               iter = NULL;
               return NULL;
            } else {
               iter->step++;
               iter->cpu = 0;
            }
         }
         iter->count = memprof_perf_count(iter->step, iter->cpu);
      }
   } while (!perf_valid(iter));

   if (iter)
      (*pos)++;

   return iter;
}

static void memprof_perf_stop(struct seq_file *p, void *v) {
   struct memprof_seq_inter *iter = (struct memprof_seq_inter *)v;
   if (iter)
      kfree(iter);
}

struct seq_operations memprof_seq_perf_ops = {
   .start = memprof_perf_start,
   .next = memprof_perf_next,
   .stop = memprof_perf_stop,
   .show = memprof_perf_show
};

static int memprof_perf_release(struct inode *inode, struct file *file) {
   int cpu;
   for_each_online_cpu(cpu) {
      struct mmap_buffer *m = per_cpu(mmap_buffers, cpu);
      struct comm_buffer *c = per_cpu(comm_buffers, cpu);
      struct task_buffer *t = per_cpu(task_buffers, cpu);
      m->count = 0;
      c->count = 0;
      t->count = 0;
   }
   return seq_release(inode, file);
}

static int memprof_perf_data_open(struct inode *inode, struct file *file) {
   return seq_open(file, &memprof_seq_perf_ops);
}

const struct file_operations memprof_perf_data_fops = {
   .owner		= THIS_MODULE,
   .open           = memprof_perf_data_open,
   .read           = seq_read,
   .llseek         = seq_lseek,
   .release        = memprof_perf_release,
};


/***
 * /proc/memprof_ibs
 */
struct memprof_seq_inter {
   int cpu;
   int e;
   int cpu_count;
   int e_count;
};

static int memprof_seq_raw_show(struct seq_file *m, void *v)
{
   struct memprof_seq_inter *iter = (struct memprof_seq_inter *)v;
   struct sample_buffer *b;
   struct s *s;

   if (!iter) {
      return 0;
   }

   if(iter->cpu == 0 && iter->e == 1) {
      int r, j;
      struct i i = { .max_nodes = num_possible_nodes(), .sampling_rate = max_cnt_op };
      struct h h = {
         .version = S_VERSION,
      };
      r = seq_write(m, &h, sizeof(h));
      if(r)
         printk("Error writing first header\n");

      strncpy(i.hostname, current->nsproxy->uts_ns->name.nodename, sizeof(i.hostname));
      i.node_begin = kmalloc(sizeof(*i.node_begin)*num_possible_nodes(), GFP_KERNEL);
      i.node_end = kmalloc(sizeof(*i.node_end)*num_possible_nodes(), GFP_KERNEL);
      for(j = 0; j < num_possible_nodes(); j++) {
         if(!NODE_DATA(j))
            continue;
         i.node_begin[j] = node_start_pfn(j);
         i.node_end[j] =  node_end_pfn(j);
      }
      r = seq_write(m, &i, sizeof(i));
      if(r)
         printk("Error writing second header\n");
      kfree(i.node_begin);
      kfree(i.node_end);
   }

   b = per_cpu(sample_buffers, iter->cpu);
   s = &b->samples[iter->e];
   return (seq_write(m, s, sizeof(*s)) == 0)?0:-1;
}

static inline int iter_valid(struct memprof_seq_inter *iter) {
   if (iter->cpu < iter->cpu_count && iter->e < iter->e_count)
      return 1;
   else
      return 0;
}

static inline int iter_step(struct memprof_seq_inter *iter) {
   do {
      iter->e++;
      if (iter->e >= iter->e_count) {
         iter->e = 0;
         iter->cpu++;
         if (iter->cpu >= num_possible_cpus())
            return 0;
         iter->e_count = per_cpu(sample_buffers, iter->cpu)->count;
      }
   } while (!iter_valid(iter));
   return 1;
}

static inline int pos_to_iter(loff_t pos, struct memprof_seq_inter *iter) {
   iter->cpu = 0;
   iter->cpu_count = num_possible_cpus();
   iter->e = 0;
   iter->e_count = per_cpu(sample_buffers, iter->cpu)->count;

   while (true) {
      if (iter->e_count <= pos) {
         pos -= iter->e_count;
         iter->cpu++;
         iter->e = 0;
         iter->e_count = per_cpu(sample_buffers, iter->cpu)->count;
      } else {
         iter->e = pos;
         return iter_valid(iter);
      }
   }
}

static void *memprof_seq_start(struct seq_file *p, loff_t *pos) {
   struct memprof_seq_inter *iter = kmalloc(sizeof(struct memprof_seq_inter), GFP_KERNEL);
   if (!iter)
      return ERR_PTR(-ENOMEM);

   if (!pos_to_iter(*pos, iter) || !iter_step(iter)) {
      kfree(iter);
      iter = NULL;
   }

   return iter;
}

static void *memprof_seq_next(struct seq_file *p, void *v, loff_t *pos) {
   struct memprof_seq_inter *iter = (struct memprof_seq_inter *)v;

   if (!iter_step(iter)) {
      kfree(iter);
      iter = NULL;
   }

   if (iter)
      (*pos)++;

   return iter;
}

static void memprof_seq_stop(struct seq_file *p, void *v) {
   struct memprof_seq_inter *iter = (struct memprof_seq_inter *)v;
   if (iter)
      kfree(iter);
}

static int memprof_data_release(struct inode *inode, struct file *file) {
   int cpu;
   for_each_online_cpu(cpu) {
      struct sample_buffer *sb = per_cpu(sample_buffers, cpu);
      sb->count = 0;
   }
   return seq_release(inode, file);
}

struct seq_operations memprof_seq_raw_ops = {
   .start = memprof_seq_start,
   .next = memprof_seq_next,
   .stop = memprof_seq_stop,
   .show = memprof_seq_raw_show
};

static int memprof_raw_data_open(struct inode *inode, struct file *file) {
   return seq_open(file, &memprof_seq_raw_ops);
}

const struct file_operations memprof_raw_data_fops = {
   .owner		= THIS_MODULE,
   .open           = memprof_raw_data_open,
   .read           = seq_read,
   .llseek         = seq_lseek,
   .release        = memprof_data_release,
};









