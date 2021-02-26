#!/bin/bash

# Copyright (C) 2020-2021, Silexica GmbH, Lichtstr. 25, Cologne, Germany
# All rights reserved

set -e
vitis_hls -f run_hls.tcl
top=`sed -n 's%^set_top %%p' run_hls.tcl`
proj=`sed -n 's%^open_project -reset %%p' run_hls.tcl`
for i in $proj/*/syn/report/${top}_csynth.rpt; do
  solution=`echo $i|sed 's%^[^[/]*/\([^/]*\).*%\1%'`
  if [ $solution = "3.vanilla" ]; then
    echo "##*****************************Switching to 85MHz*********************************##"
  fi
  echo -n '## '
  sed -n "s%^SOLUTION \($solution:.*\)%\1%p" vitis_hls.log
  sed -n -e '/Loop:/,/^$/ {/\(Loop:\|---\)/ d; p}' -e '/^== Utilization/,/^|Total/ {/\(Name\|Total\)/ p}' $i
  echo
done|sed 's%\<stp\>%yes%g' >latency.new
diff -u latency.ref latency.new || {
  if [ "$UPDATE_REFERENCES" = 1 ]; then
    echo "Reference update required and applied as UPDATE_REFERENCES=1"
    cp latency.new latency.ref
  else
    echo "Reference update required (set UPDATE_REFERENCES=1 to auto cp latency.new latency.ref)"
    exit 1
  fi
}
cmp $proj.slxopts{,.ref}
echo OK
