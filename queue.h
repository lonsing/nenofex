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

#ifndef _QUEUE_H_
#define _QUEUE_H_

typedef struct Queue Queue;

struct Queue
{
  void **elems;
  void **end;
  void **first;
  void **last;
};


Queue *create_queue (unsigned int size);

void delete_queue (Queue * queue);

void enqueue (Queue * queue, void *elem);

void *dequeue (Queue * queue);

unsigned int size_queue (Queue * queue);

unsigned int count_queue (Queue * queue);

void reset_queue (Queue * queue);

#endif /* _QUEUE_H_ */