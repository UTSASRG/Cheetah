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
 * @file   cacheinfo.h
 * @brief  Detailed information about read/write access on specified cache. 
 * @author Tongping Liu <http://www.cs.utsa.edu/~tongpingliu>
 */ 


#ifndef _CACHEINFO_H_
#define _CACHEINFO_H_

#include <set>
#include <list>
#include <vector>

#if !defined(_WIN32)
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include <algorithm> //sort
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xdefines.h"
#include "cachehistory.h"

using namespace std;

class cacheinfo {
public:
  cacheinfo() 
  {
  }
  
  ~cacheinfo (void) {
  }

  void initialize(void * start, size_t sz) {
    if(start == NULL) {
      assert(0);
    }

    _cacheStart = (char *)start;
    _cacheEnd = _cacheStart + sz;

    // See explanation of cachehistory.
    // we want to avoid the checking of empty table
    // so it is possible that eery cache line will have invalidation
    // even it is not happening.
    _invalidations = -1;
    _history.init();
  }

  // Check the cache line level's invalidation
  void handleAccess(int tid, char * addr, eAccessType type) {
    // In the prediction, cacheStart and cacheEnd is not irregular
    // we only update corresponding cache line.
    if(addr >= _cacheStart && addr <= _cacheEnd) {
      if(_history.updateHistory(tid, type)) {
        _invalidations++;
      }
			
    }
  }

  long invalidations(void) {
    return _invalidations;
  }

  char * getCacheStart(void) {
    return _cacheStart;
  }

private:
  // How many invalidations happens in this cache line.
  //unsigned long  _invalidations;
  long  _invalidations;
  cachehistory _history;
  char * _cacheStart;
  char * _cacheEnd;
};

#endif

