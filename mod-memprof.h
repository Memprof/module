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
#ifndef _MOD_MH
#define _MOD_MH 1

#define NUM_CPU_SAMPLES 1000000

struct sample_buffer {
   int count;
   int overflow;
   struct s samples[NUM_CPU_SAMPLES];
};
extern DEFINE_PER_CPU(struct sample_buffer *, sample_buffers);
extern int max_cnt_op;

#endif
