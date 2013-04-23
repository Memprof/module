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
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/textsearch.h>
#include <linux/memory.h>
#include "hijack.h"
#include "hooks.h"

/*
 * Overload some kernel functions.
 * Before a page_rw + a memcpy used to be enough, but now we have to use fixmaps.
 * The kernel provides a function to do that(text_poke). This function has to be called under text_mutex.
 */
 
void intercept_init(struct hijack* h, void *orig, void *new)
{
   h->orig = orig;
   memcpy( h->jump_code, "\x48\xb8\x00\x00\x00\x00\x00\x00\x00\x00\xff\xe0", CODESIZE);
   *(long *)&h->jump_code[2] = (long)new;
   memcpy( h->original_code, orig, CODESIZE );
   return;
}

int set_memory_ro(unsigned long addr, int numpages);
int set_memory_rw(unsigned long addr, int numpages);
int set_memory_nx(unsigned long addr, int numpages);
void intercept_start(struct hijack *h)
{
   set_memory_rw(((long)h->orig) & PAGE_MASK, 1);
   set_memory_rw((((long)h->orig) + CODESIZE) & PAGE_MASK, 1);
   mutex_lock(text_mutex_hook);
   text_poke_hook(h->orig, h->jump_code, CODESIZE);
   mutex_unlock(text_mutex_hook);
   set_memory_ro(((long)h->orig) & PAGE_MASK, 1);
   set_memory_ro((((long)h->orig) + CODESIZE) & PAGE_MASK, 1);
}

void intercept_stop(struct hijack *h)
{
   set_memory_rw(((long)h->orig) & PAGE_MASK, 1);
   set_memory_rw((((long)h->orig) + CODESIZE) & PAGE_MASK, 1);
   mutex_lock(text_mutex_hook);
   text_poke_hook(h->orig, h->original_code, CODESIZE);
   mutex_unlock(text_mutex_hook);
   set_memory_ro(((long)h->orig) & PAGE_MASK, 1);
   set_memory_ro((((long)h->orig) + CODESIZE) & PAGE_MASK, 1);
}

