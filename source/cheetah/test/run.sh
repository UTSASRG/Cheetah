#!/bin/sh

export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:~/libpfm/lib
time LD_PRELOAD=../libdefault64.so ./test
