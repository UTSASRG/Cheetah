#!/bin/sh

#export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:~./falsesharing/cheetah
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/home/tongpingliu/falsesharing/cheetah
#:~/libpfm/lib
LD_PRELOAD=/home/tongpingliu/falsesharing/cheetah/libdefault64.so ./test
