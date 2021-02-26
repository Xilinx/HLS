# Copyright (C) 2020, Silexica GmbH, Lichtstr. 25, Cologne, Germany
# All rights reserved

set slxpluginhome [file normalize $::env(SLX_VITIS_PLUGIN_HOME)]

proc run_the_design {passes} {
    global slxpluginhome
    # Source x_hls.tcl to determine which steps to execute
    source x_hls.tcl

    set ::LLVM_CUSTOM_CMD [concat {$LLVM_CUSTOM_OPT -load } $slxpluginhome/lib/slxplugin.so $passes { -slx-remove-directives -pass-remarks=slx-loop-interchange -pass-remarks-analysis=slx-loop-interchange -pass-remarks-missed=slx-loop-interchange $LLVM_CUSTOM_INPUT -o $LLVM_CUSTOM_OUTPUT }]
    if {$hls_exec > 0} {
    	# Also synthesislxpluginhome/lib
  	  csynth_design
    }
    if {$hls_exec > 1} {
    	# Also RTL Simulation
    	cosim_design
    }
    if {$hls_exec > 2} { 
    	# Also RTL implementation
    	export_design
    }
    if {$hls_exec > 3} { 
      # nothing to add
    }
}

proc request_solution {name clock_period descr} {
  puts "SOLUTION $name: $descr (clock: $clock_period)"
  open_solution -reset $name
  # Define technology and clock rate
  set_part  {xcvu9p-flga2104-2-i}
  create_clock -period $clock_period
  config_rtl -reset all -reset_async
}

# Create a project
open_project -reset proj

# Add design files
add_files mri_vhls.c -cflags [concat -fno-math-errno -include $slxpluginhome/include/slxplugin.h]
# Add test bench & files
add_files -tb mri_vhls_tb.cpp

# Set the top-level function
set_top mri_fk

# ########################################################
request_solution 0.vanilla 333MHz "no LLVM_CUSTOM_CMD, no Xilinx directives"

# Check the testbench passes
csim_design

run_the_design ""

# ########################################################
request_solution 1.nointerchange 333MHz "LLVM_CUSTOM_CMD -slx-prepare-interchange etc., no Xilinx directives"
run_the_design "-slx-prepare-for-interchange"

# ########################################################
request_solution 2.interchange 333MHz "LLVM_CUSTOM_CMD -slx-prepare-interchange etc. -slx-loop-interchange, no Xilinx directives"
run_the_design "-slx-prepare-for-interchange -slx-loop-interchange"

exit
