# Copyright (C) 2020-2021, Silexica GmbH, Lichtstr. 25, Cologne, Germany
# All rights reserved

# This creates different optimization solutions for the loop nest in code.cpp.


# Create a project
open_project -reset proj
# add_files with access to SLX directive via "-include $slxplugin/include/slxplugin.h"
add_files code.cpp -cflags "-include $::slxplugin/include/slxplugin.h"
add_files -tb code_test.cpp
set_top reduce_2d_to_1d

# Solutions to compare:
# - vanilla: Vitis HLS defaults, deleting any SLX directives
# - nointerchange: run cleanup passes for the SLX directives, but then delete the directives
# - interchange: run cleanup passes and inplement the SLX directives
#
# Experiments run with different clock speeds to change the latency of
# `+` (DAddSub_fulldspi).
# - clock 85MHz:  DAddSub_fulldspi has a 1 cycle latency
# - clock 333MHz: DAddSub_fulldspi has a 7 cycle latency

# Helper to announce and create a solution
proc ::request_solution {name clock_period descr} {
  puts "SOLUTION $name: $descr (clock: $clock_period)"
  open_solution -reset $name
  # Define technology and clock rate
  set_part  {xcvu9p-flga2104-2-i}
  create_clock -period $clock_period
}

# ########################################################
request_solution 0.vanilla 333MHz "no LLVM_CUSTOM_CMD, no Xilinx directives"
csim_design
csynth_design

# ########################################################
request_solution 1.nointerchange 333MHz "LLVM_CUSTOM_CMD -slx-prepare-interchange, no Xilinx directives"
set_slx_plugin_options -slx-prepare-for-interchange
csynth_design

# ########################################################
request_solution 2.interchange 333MHz "LLVM_CUSTOM_CMD -slx-prepare-interchange -slx-loop-interchange, no Xilinx directives"
set_slx_plugin_options -slx-prepare-for-interchange -slx-loop-interchange
csynth_design

# ########################################################
request_solution 3.vanilla 85MHz "no LLVM_CUSTOM_CMD, no Xilinx directives"
csynth_design

# ########################################################
request_solution 4.nointerchange 85MHz "LLVM_CUSTOM_CMD -slx-prepare-interchange, no Xilinx directives"
set_slx_plugin_options -slx-prepare-for-interchange
csynth_design

# ########################################################
request_solution 5.interchange 85MHz "LLVM_CUSTOM_CMD -slx-prepare-interchange -slx-loop-interchange, no Xilinx directives"
set_slx_plugin_options -slx-prepare-for-interchange -slx-loop-interchange
csynth_design
exit
