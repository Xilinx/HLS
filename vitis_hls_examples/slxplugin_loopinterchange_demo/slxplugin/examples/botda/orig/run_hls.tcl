# Copyright (C) 2020, Silexica GmbH, Lichtstr. 25, Cologne, Germany
# All rights reserved

set slxpluginhome [file normalize $::env(SLX_VITIS_PLUGIN_HOME)]

proc run_the_design {passes} {
  global slxpluginhome
  # Source x_hls.tcl to determine which steps to execute
  source x_hls.tcl

  set ::LLVM_CUSTOM_CMD [concat {$LLVM_CUSTOM_OPT -load } $slxpluginhome/lib/slxplugin.so $passes { -slx-remove-directives $LLVM_CUSTOM_INPUT -o $LLVM_CUSTOM_OUTPUT }]

  if {$hls_exec == 1} {
    # Run Synthesis and Exit
    csynth_design

  } elseif {$hls_exec == 2} {
    # Run Synthesis, RTL Simulation and Exit
    csynth_design

    cosim_design
  } elseif {$hls_exec == 3} {
    # Run Synthesis, RTL Simulation, RTL implementation and Exit
    csynth_design

    cosim_design
    export_design
  } else {
    # Default is to exit after setup
    csynth_design
  }
}

proc request_solution {name clock_period} {
  open_solution -reset $name
  # Define technology and clock rate
  set_part  {xcvu9p-flga2104-2-i}
  create_clock -period $clock_period
}

#open_project proj
#proc Skip {} {
# Create a project
open_project -reset proj

# Add design files
add_files botda.cpp -cflags [concat -fno-math-errno -include $slxpluginhome/include/slxplugin.h]
# Add test bench & files
add_files -tb botda_test.cpp

# Set the top-level function
set_top linearSVR

# vanilla version, without any interchange or preparation
request_solution 0.vanilla 333MHz
csim_design
run_the_design ""

# prepared version, some preparation passes are used but no interchange is applied
request_solution 1.nointerchange 333MHz
run_the_design "-slx-prepare-for-interchange"

# Interchanged version.
request_solution 2.interchange 333MHz
run_the_design "-slx-prepare-for-interchange -slx-loop-interchange -slx-fatal-unapplied-interchange=FALSE -pass-remarks-missed=slx-loop-interchange"

exit
