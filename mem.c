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

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include "mem.h"


static size_t cur_bytes = 0;
static size_t max_bytes = 0;


void *
mem_malloc (size_t bytes)
{
  void *result;

  result = malloc (bytes);

  if (!result)
    {
      fprintf (stderr, "ERROR - mem: malloc failed!\n");
      abort ();
    }
  cur_bytes += bytes;

  if (cur_bytes > max_bytes)
    max_bytes = cur_bytes;

  return result;
}


void *
mem_realloc (void *ptr, size_t old_bytes, size_t new_bytes)
{
  void *result;

  if (!ptr)
    {
      assert (!old_bytes);
      return mem_malloc (new_bytes);
    }

  if (!new_bytes)
    {
      mem_free (ptr, old_bytes);
      return 0;
    }

  assert (cur_bytes >= old_bytes);
  cur_bytes -= old_bytes;
  cur_bytes += new_bytes;
  result = realloc (ptr, new_bytes);

  if (!result)
    {
      fprintf (stderr, "ERROR - mem: realloc failed!\n");
      abort ();
    }

  if (cur_bytes > max_bytes)
    max_bytes = cur_bytes;

  return result;
}


void
mem_free (void *ptr, size_t bytes)
{
  if (!ptr)
    {
      fprintf (stderr, "ERROR - mem: free at null pointer!\n");
      abort ();
    }

  assert (cur_bytes >= bytes);
  free (ptr);
  cur_bytes -= bytes;
}


size_t
get_cur_bytes ()
{
  return cur_bytes;
}


size_t
get_max_bytes ()
{
  return max_bytes;
}


void
mem_check ()
{
  if (cur_bytes)
    {
      fprintf (stderr, "ERROR - mem: cur_bytes = %ld, but expected 0!\n",
               cur_bytes);
      abort ();
    }
}
