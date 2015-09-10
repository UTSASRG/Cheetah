#!/usr/bin/python

import os
import sys
import subprocess
import re

benchmark="boost"
runs = 10
runtime = 0;

for n in range(0, runs):
  print 'Running '+benchmark+'.'+ str(n);

  os.system("sudo sh -c \"sync; echo 3 > /proc/sys/vm/drop_caches\";");
  start_time = os.times()[4]

  p = subprocess.Popen(['./runsysbench.sh'], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
  p.wait()

  time = os.times()[4] - start_time
  runtime = runtime + time;

print benchmark + ":" + str(runtime/runs);

