.. title:: clang-tidy - xilinx-top-parameters

xilinx-top-parameters
=====================

This check errors on unsupported parameter and return type of a Xilinx
top-level function.


Check Return Type
-----------------

* Contains `ap_int/ap_uint/ap_fixed/ap_ufixed` type having a non-power of 2 or
  smaller than 8 bit size.

  For example, the `ap_int<5>` return type shown below.

  .. code-block:: c++
  
    ap_int<5> top_level_func (/*...*/) {
    #pragma HLS top
    //...
    }

  [TODO] Add why we need to check this

* Is an arbitrary-precision integer

  The arbitrary-precision integer type happens when you use the attribute
  `__attribute__((bitwidth(n)))` with `n` that is not 8, 16, 32, or 64.

  For example, the `int` return type below with an attribute to specify the 22
  bit width for the type.

  .. code-block:: c++
  
    __attribute__((bitwidth(22))) int top_level_func (/*...*/) {
    #pragma HLS top
    //...
    }

  A common mistakes here is to use `__attribute__((bitwidth(128)))` but
  `int128` is not yet support by Xilinx in this case.

  [TODO] Add why we need to check this


Check Parameter Type
--------------------

* Contains `ap_int/ap_uint/ap_fixed/ap_ufixed` type having a non-power of 2 or
  smaller than 8 bit size

  Same as the check for return type of the top-level function. In the following
  example, parameter `V` is a `ap_uint` type with 27 bit width size. This will
  be error out in the check.

  .. code-block:: c++
  
    int top_level_func (ap_uint<27> V) {
    #pragma HLS top
    //...
    }

  [TODO] Add why we need to check this

* Is an arbitrary-precision integer

  This is the same check as the one for return type of the top-level function.

  [TODO] Add why we need to check this

* `struct` or `class` parameter contains pointer type value inside

  Top-level function is run on the Xilinx FPGA and there are no unified memory
  view shared between CPU(host) and FPGA. Xilinx does not support the mapping
  for the pointer in `struct` or `class`.

  Parameter `V` is a pointer points to structure `t`. And since `t` contains a
  int pointer type value `a`. This will be error out in the check.

  .. code-block:: c++
  
    template <typename T>
    struct t {
      q<T> a;
    };
    int top_level_func (t<int *> *V) {
    #pragma HLS top
    //...
    }

* Parameter is a hls stream array

  For example, `Arr` is a hls stream array and is not support as a parameter
  passed to the top-level function.

  .. code-block:: c++
  
    int top_level_func (hls::stream<int> Arr[2]) {
    #pragma HLS top
    //...
    }

  [TODO] Add why we need to check this

* `struct` or `class` parameter contains hls stream type value inside

  For example, parameter `xx` is a reference type of structure `x1`. And `x1`
  contains hls stream variable.

  .. code-block:: c++
  
    struct x1 {
      hls::stream<int> a;
    };
    int top_level_func (x1 &xx) {
    #pragma HLS top
    //...
    }

  [TODO] Add why we need to check this

* Array partition/reshape pragma can not be used on parameters

  Since HLS `array_partition` and `array_reshape` pragma support for Array
  stored in on-chip memory on FPGA, such as BRAM. We can not pass parameters
  with these pragamas applied on them. For example, `A` in the below example.

  .. code-block:: c++
  
    int top_level_func (int A[2]) {
    #pragma HLS top
    #pragma HLS array_partition variable = A complete
    //...
    }

