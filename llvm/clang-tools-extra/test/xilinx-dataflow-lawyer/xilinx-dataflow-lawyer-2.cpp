// RUN: cp -f %s   %T/test2.cpp
// RUN: xilinx-dataflow-lawyer %T/test2.cpp -- -fhls -fstrict-dataflow 2>&1 | FileCheck %s --check-prefix=CHECK-MESSAGES
// RUN: FileCheck --check-prefix=CHECK-FIXES -input-file=%T/test2.cpp %s

#define M 10

void sub1(int a[M], int s[M], int k, int p);
void sub2(int s[M], int b[M], int &t) {
  for (int i = 1; i < t; i++) {
    #pragma HLS dataflow
    //CHECK-MESSAGES: Either use an argument of the function or declare the variable inside the dataflow loop body
    sub1(s, b, i, i+1);
    b[i] = s[i];
    b[i] = s[i];
    b[i] = s[i];
    b[i] = s[i];
    //CHECK-MESSAGES: Since the only kind of statements allowed in a canonical dataflow region are variable declarations and function calls, the compiler may not be able to correctly handle the region
    //CHECK-MESSAGES: Since the only kind of statements allowed in a canonical dataflow region are variable declarations and function calls, the compiler may not be able to correctly handle the region
    //CHECK-MESSAGES: Since the only kind of statements allowed in a canonical dataflow region are variable declarations and function calls, the compiler may not be able to correctly handle the region
    //CHECK-MESSAGES: There are a total of 4 such instances of non-canonical statements in the dataflow region
  }
}


void dut(int a[M], int b[M], int &e, int f, int g) {
#pragma HLS dataflow
  //CHECK-FIXES: #pragma HLS dataflow
  //CHECK-MESSAGES: Static scalars and arrays declared inside a dataflow region will be treated like local variables
  static int s[M], t;
  //CHECK-MESSAGES: Either use an argument of the function or declare the variable inside the dataflow loop body
  //CHECK-MESSAGES: Either use an argument of the function or declare the variable inside the dataflow loop body
  sub1(a, s, f + g, f - g);
  sub2(s, b, t);
  e = t;
  e = t;
  e = t;
  e = t;
  e = t;
  //CHECK-MESSAGES: Since the only kind of statements allowed in a canonical dataflow region are variable declarations and function calls, the compiler may not be able to correctly handle the region
  //CHECK-MESSAGES: Since the only kind of statements allowed in a canonical dataflow region are variable declarations and function calls, the compiler may not be able to correctly handle the region
  //CHECK-MESSAGES: Since the only kind of statements allowed in a canonical dataflow region are variable declarations and function calls, the compiler may not be able to correctly handle the region
  //CHECK-MESSAGES: There are a total of 5 such instances of non-canonical statements in the dataflow region
  return;
}
