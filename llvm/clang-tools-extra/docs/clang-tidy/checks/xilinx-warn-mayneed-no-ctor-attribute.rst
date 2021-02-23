.. title:: clang-tidy - xilinx-warn-mayneed-no-ctor-attribute

xilinx-warn-mayneed-no-ctor-attribute
=====================================

This check warns on std::complex and array of std::complex vairable that is not
declared with __attribute__((no_ctor)) in a dataflow region.

For now, this check only supports strict dataflow form.
