.. title:: clang-tidy - xilinx-array-stream-check

xilinx-array-stream-check
=========================

xilinx-array-stream-check checks the function parameters which are
arrays/pointers/references. These arrays/pointers/references are implying
stream (axis/ap_fifo) interfaces in HLS.
We require those arrays to be volatile such that we can prevent
compiler optimizations (e.g. instruction reordering/deletion etc.)
that are illegal for HLS stream accesses.

Examples
--------------------

* Example below shows a top function with parameters in, out to be ap_fifo interface.
  We requires volatile qulifiers for these array in order to prevent compiler
  optimizations on the array stream.

  .. code-block:: c++
  void f(unsigned in[1024], unsigned out[1024]) {
  #pragma HLS INTERFACE ap_fifo port=in
  #pragma HLS INTERFACE ap_fifo port=out
  #pragma HLS TOP
  }

After applying clang-tidy's fixes:

  .. code-block:: c++
  void f(volatile unsigned in[1024], volatile unsigned out[1024]) {
  #pragma HLS INTERFACE ap_fifo port=in
  #pragma HLS INTERFACE ap_fifo port=out
  #pragma HLS TOP
  }

Note, hls::stream does not require to be volatile.





