#!/bin/bash

# Copyright (C) 2020, Silexica GmbH, Lichtstr. 25, Cologne, Germany
# All rights reserved

set -e
vitis_hls -f run_hls.tcl
for i in */*/syn/report/linearSVR_csynth.rpt; do
  echo $i|sed 's%^[^[/]*/\([^/]*\).*%## \1%'
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
echo OK
