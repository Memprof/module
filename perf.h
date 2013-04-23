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

#ifndef _PERF_H
#define _PERF_H 1

#define NUM_MMAP_SAMPLES 100000

struct mmap_buffer {
   int count;
   int overflow;
   struct mmap_event samples[NUM_MMAP_SAMPLES];
};

struct task_buffer {
   int count;
   int overflow;
   struct task_event samples[NUM_MMAP_SAMPLES];
};

struct comm_buffer {
   int count;
   int overflow;
   struct comm_event samples[NUM_MMAP_SAMPLES];
};

extern DEFINE_PER_CPU(struct mmap_buffer*, mmap_buffers);
extern DEFINE_PER_CPU(struct task_buffer*, task_buffers);
extern DEFINE_PER_CPU(struct comm_buffer*, comm_buffers);

void init_hooks(void);
void set_hooks(void);
void clear_hooks(void);
void free_perf_buffers(void);
int alloc_perf_buffers(void);

#endif
