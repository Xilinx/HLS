# Copyright (C) 2021, Silexica GmbH, Lichtstr. 25, Cologne, Germany
# All rights reserved

# Extend the Vitis HLS commands to the SLX Plugin options can
# be specified per solution by:
# - add a command `set_slx_plugin_options` to record the
#   SLX Plugin options to use for a solution
# - extending `add_project` to source <project-path>.slxopts
# - extend `csynth_design` to apply the requested SLX
#   Plugin options via LLVM_CUSTOM_CMD

# Work out where the SLX plugin is.
if {[info exists ::env(SLX_VITIS_PLUGIN_HOME)]} {
  set ::slxplugin [file normalize $::env(SLX_VITIS_PLUGIN_HOME)]
} else {
  # If running from the demo location, then check the related
  # plugins folder.
  if {[file exists slxplugin/lib/slxplugin.so]} {
    set ::slxplugin [file normalize slxplugin]
  } else {
    error "SLX_VITIS_PLUGIN_HOME is not set - please check your SLX Plugin User Guide"
  }
}
puts "slxplugin=$::slxplugin"
if { ! [file exists "$::slxplugin/lib/slxplugin.so"] } {
  error "\$SLX_VITIS_PLUGIN_HOME/lib/slxplugin.so is missing - please check your SLX Plugin installation"
}

# The dictionary where the per-solution SLX plugin options
# are stored. 
set ::slx_solution_options [dict create]
# The file containing additional SLX plugin settings
set slx_settings_file ""

# Extend `add_project` to:
# - clear the `slx_solution_options`
# - source `<project-path>.slxopts` (if it is present)
rename ::open_project ::xilinx_open_project
proc ::open_project args {
  ::xilinx_open_project {*}$args

  # always reset the options when opening a project
  set ::slx_solution_options [dict create]

  set ::slx_settings_file "[get_project -directory].slxopts"
  if {[file exists $::slx_settings_file]} {
    puts "INFO: sourcing $::slx_settings_file"
    source $::slx_settings_file
  }
}

# Extend `csynth_design` to see if `slx_solution_options` has an entry
# for the current solution and, if so, add those to the LLVM_CUSTOM_CMD.
rename ::csynth_design ::xilinx_csynth_design
proc ::csynth_design args {
  # See if SLX Plugin options have been requested for this solution
  if {[dict exists $::slx_solution_options [get_solution]]} {
    set slx_plugin_options [dict get $::slx_solution_options [get_solution]]
  } else {
    set slx_plugin_options ""
  }
  set ::LLVM_CUSTOM_CMD [concat \
      {$LLVM_CUSTOM_OPT -load} $::slxplugin/lib/slxplugin.so \
      $slx_plugin_options -slx-remove-directives \
      {$LLVM_CUSTOM_INPUT -o $LLVM_CUSTOM_OUTPUT}]

  uplevel 1 ::xilinx_csynth_design {*}$args
}

# New command to record the SLX Plugin options to use for
# a solution.
# - If the first argument does not begin with `-` then the
#   rest of the arguments are recorded as options for the solution
#   with name=first argument.
# - Otherwise the arguments are recorded as the options for
#   the current solution.
# - Calling the command again for the same solution replaces
#   the previous options.
proc ::set_slx_plugin_options args {
  # if the first argument does not have a leading '-' then
  # use it as the name of the solution
  if {[string compare -length 1 [lindex $args 0] "-"]} {
    set solution [lindex $args 0]
    set args [lreplace $args 0 0]
  } else {
    set solution [get_solution]
  }
  dict set ::slx_solution_options $solution "$args"

  # Record the setting into `<project>.slxopts` if it is not
  # already in there. (This makes a generated project
  # work out-of-the-box in the GUI.)
  set data {}
  set outdata {}
  set add_option 1
  if {[file exists $::slx_settings_file]} {
    set fp [open $::slx_settings_file r]
    set file_data [read -nonewline $fp]
    close $fp
    set data [split $file_data "\n"]
    foreach line $data {
      set words [split $line " "]
      if {[lrange $words 0 1] == [list "set_slx_plugin_options" $solution]} {
        if {$args != [lrange $words 2 end]} {
          set add_option 1
          continue
        }
        # keeping a line with the right options
        set add_option 0
      }
      lappend outdata $line
    }
  }
  if {$add_option} {
    lappend outdata "set_slx_plugin_options $solution $args"
  }
  if {$data != $outdata} {
    set fp [open $::slx_settings_file w]
    foreach line $outdata {
      puts $fp $line
    }
    close $fp
  }
}
