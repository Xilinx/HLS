// RUN: cp -f %s   %T/test.c
// RUN: xilinx-dataflow-lawyer %T/test.c -- -fhls -fstrict-dataflow 2>&1 | FileCheck %s --check-prefix=CHECK-MESSAGES

F0(int *in, int *out, int i);
F1(int *in, int *out, int i);

void kernel(int a[10], int c[2*10]) {
#pragma HLS DATAFLOW
//CHECK-MESSAGES: Static scalars and arrays declared inside a dataflow region will be treated like local variables
  static int b[2*10];
  F0(a, b, i);
  F1(b, c, i);
}
