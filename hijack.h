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

#ifndef _HIJACK
#define _HIJACK 1

#define CODESIZE 12

struct hijack {
   void *orig;
   unsigned char original_code[CODESIZE];
   unsigned char jump_code[CODESIZE];
};

void intercept_init(struct hijack*, void *orig, void *new);
void intercept_start(struct hijack*);
void intercept_stop(struct hijack*);

#endif
