/*                                                                                                                                                                           
Copyright (C) 2013  
Baptiste Lepers <baptiste.lepers@gmail.com>

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

#ifndef MEMPROF
#define MEMPROF 1

/*
 * Memprof binary format
 */

#define S_VERSION 8

struct h {  // header
   int version;
};

struct i { // machine specific information
   int       sorted_by_rdt;
   char      hostname[32];
   int       max_nodes;
   int       sampling_rate;
   uint64_t *node_begin;
   uint64_t *node_end;
};

struct s { // a sample
   uint64_t rdt;
   uint32_t cpu;
   uint64_t rip;
   uint32_t ibs_op_data1_high;
   uint32_t ibs_op_data1_low;
   uint32_t ibs_op_data2_low;
   uint32_t ibs_op_data3_high;
   uint32_t ibs_op_data3_low;
   uint64_t ibs_dc_linear;
   uint64_t ibs_dc_phys;
   uint32_t tid;
   uint32_t pid;
   uint32_t kern;
   char     comm[32];
   uint64_t usersp;
   uint64_t stack;
};


/*
 * Perf events binary format
 */
#define M_VERSION 2
struct m {           // header
   uint32_t nb_mmap_events;
   uint32_t nb_comm_events;
   uint32_t nb_task_events;
};

struct mmap_event {
   size_t   size;
   int      type;
   uint64_t rdt;
   char     file_name[256];
   uint32_t file_size;
   uint32_t pid;
   uint32_t tid;
   uint64_t start;
   uint64_t len;
   uint64_t pgoff;
};

struct task_event {
   size_t   size;
   int      type;
   uint64_t rdt;
   uint32_t pid;
   uint32_t ppid;
   uint32_t tid;
   uint32_t ptid;
   uint32_t new;
};

struct comm_event {
   size_t   size;
   int      type;
   uint64_t rdt;
   uint32_t pid;
   uint32_t tid;
   char     comm[32];
};

#endif
