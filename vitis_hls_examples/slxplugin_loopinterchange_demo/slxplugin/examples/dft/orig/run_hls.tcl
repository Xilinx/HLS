# Copyright (C) 2020, Silexica GmbH, Lichtstr. 25, Cologne, Germany
# All rights reserved

set slxpluginhome [file normalize $::env(SLX_VITIS_PLUGIN_HOME)]

proc run_the_design {passes} {
    global slxpluginhome
    # Source x_hls.tcl to determine which steps to execute
    source x_hls.tcl

    set ::LLVM_CUSTOM_CMD [concat {$LLVM_CUSTOM_OPT -load} $slxpluginhome/lib/slxplugin.so $passes {-slx-remove-directives $LLVM_CUSTOM_INPUT -o $LLVM_CUSTOM_OUTPUT}]
    if {$hls_exec > 0} {
    	# Also synthesis
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

proc request_solution {name} {
  open_solution -reset $name
  # Define technology and clock rate
  set_part  {xcvu9p-flga2104-2-i}
  create_clock -period 333MHz
}

# Create a project
open_project -reset proj

# Add design files
add_files code.cpp -cflags [concat -fno-math-errno -include $slxpluginhome/include/slxplugin.h]
# Add test bench & files
add_files -tb code_test.cpp
add_files -tb result.golden.dat

# Set the top-level function
set_top dft

# ########################################################
# Try without any specific control. vitis_hls flattens
# the loops and achieves an II=5 for the loop nest.
request_solution 0.vanilla

# Check the testbench passes
csim_design

run_the_design ""

# ########################################################
# Try with only loop-interchange prep passes. vitis_hls
# achievs an II-1 for loop_calc.
request_solution 1.nointerchange
run_the_design "-slx-prepare-for-interchange"

# ########################################################
# Try with prep + loop-interchange passes. loop-interchange
# fails as the loop nesting is not perfect.
request_solution 2.interchange
run_the_design "-slx-prepare-for-interchange -slx-loop-interchange -slx-fatal-unapplied-interchange=FALSE -pass-remarks-missed=slx-loop-interchange"
exit
