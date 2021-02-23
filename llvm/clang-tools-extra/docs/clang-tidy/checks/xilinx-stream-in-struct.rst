.. title:: clang-tidy - xilinx-stream-in-struct

xilinx-stream-in-struct
=======================

xilinx-stream-in-struct checks the aggregate type (e.g. sturct, array) that
contains hls::stream as field/element. Aggregate type with hls::stream
is not supported. We will give an error during the check.

Examples
--------------------

```
struct S0 {
  hls::stream<int> A;
};
```

In struct S0, there is a field A of hls::stream type which is not supported
for now.

```
hls::stream<int> Arr[10];
```

Array Arr uses hls::stream<int> as element type which is not supported.

```
void f(hls::stream<int> p[10]);
```

Function f contains parameter p which is an array of hls::stream. This
is not supported.

