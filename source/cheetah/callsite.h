// -*- C++ -*-

/*
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/

/*
 * @file   callsite.h
 * @brief  Management about callsite for heap objects.
 * @author Tongping Liu <http://www.cs.utsa.edu/~tongpingliu>
 */ 
    
#ifndef SHERIFF_CALLSITE_H
#define SHERIFF_CALLSITE_H

#include <link.h>
#include <stdio.h>
#include <cassert>
#include <setjmp.h>
#include <execinfo.h>

extern "C" {
/* Define the stack_frame layout */
struct stack_frame {
  struct stack_frame * prev;/* pointing to previous stack_frame */
  long   caller_address;/* the address of caller */
};

/* pointing to the stack_bottom from libc */
extern void * __libc_stack_end;

static int back_trace(long stacks[ ], int size)
{
  void * stack_top;/* pointing to current API stack top */
  struct stack_frame * current_frame;
  int    i, found = 0;

  /* get current stack-frame */
  current_frame = (struct stack_frame*)(__builtin_frame_address(0));
  
  stack_top = &stack_top;/* pointing to curent API's stack-top */
  
  /* Omit current stack-frame due to calling current API 'back_trace' itself */
  for (i = 0; i < 1; i++) {
    if (((void*)current_frame < stack_top) || ((void*)current_frame > __libc_stack_end)) break;
    current_frame = current_frame->prev;
  }
  
  /* As we pointing to chains-beginning of real-callers, let's collect all stuff... */
  for (i = 0; i < size; i++) {
    /* Stop in case we hit the back-stack information */
    if (((void*)current_frame < stack_top) || ((void*)current_frame > __libc_stack_end)) break;
    /* omit some weird caller's stack-frame info * if hits. Avoid dead-loop */
    if ((current_frame->caller_address == 0) || (current_frame == current_frame->prev)) break;
    /* make sure the stack_frame is aligned? */
    if (((unsigned long)current_frame) & 0x01) break;

    /* Ok, we can collect the guys right now... */
    stacks[found++] = current_frame->caller_address;
		fprintf(stderr, "i %d current_frame %lx\n", i, current_frame->caller_address);
    /* move to previous stack-frame */
    current_frame = current_frame->prev;

  }

  /* omit the stack-frame before main, like API __libc_start_main */
  if (found > 1) found--;

  stacks[found] = 0;/* fill up the ending */

  return found;
}
};


class CallSite {
public:
  CallSite() {
    for (int i = 0; i < CALL_SITE_DEPTH; i++) {
      _callsite[i] = 0;
    }
  }

  unsigned long getItem(unsigned int index)
  {
    assert (index < CALL_SITE_DEPTH);
    return _callsite[index];
  }

  unsigned long getDepth() 
  {
    return CALL_SITE_DEPTH;
  }

  void print()
  {
    printf("ALLOCATION CALL SITE: ");
    for(int i = 0; i < CALL_SITE_DEPTH; i++) {
      printf("%lx\t", _callsite[i]);
    }
    printf("\n");
  }


  void fetch(void) {
    long array[10];
    int size;
    int log = 0;

  //  PRDBG("Try to get backtrace with array %p\n", array);
    if(!isBacktrace) {
      isBacktrace = true;
      // get void*'s for all entries on the stack
      size = back_trace(array, 11);
      isBacktrace = false;
   
      for(int i = 0; i < size; i++) {
        unsigned long addr = (unsigned long)array[i];
        //fprintf(stderr, "textStart %lx textEnd %lx addr %lx at log %d\n", textStart, textEnd, addr, log);
        if(addr >= textStart && addr <= textEnd) {
          _callsite[log++] = addr;
          if(log == CALL_SITE_DEPTH) {
            break;
          }
        }
      }
    }
  }

  unsigned long _callsite[CALL_SITE_DEPTH];
};

#endif
