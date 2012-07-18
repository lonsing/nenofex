/*
 This file is part of Nenofex.

 Nenofex, an expansion-based QBF solver for negation normal form.        
 Copyright 2008, 2012 Florian Lonsing.

 Nenofex is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or (at
 your option) any later version.

 Nenofex is distributed in the hope that it will be useful, but
 WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with Nenofex.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef _MEM_H_
#define _MEM_H_

#include <stddef.h>


void *mem_malloc (size_t bytes);

void mem_free (void *ptr, size_t bytes);

void *mem_realloc (void *ptr, size_t old_bytes, size_t new_bytes);

size_t get_cur_bytes ();

size_t get_max_bytes ();

void mem_check ();

#endif /* _MEM_H_ */
