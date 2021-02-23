// RUN: cp -f %s   %T/test.cpp
// RUN: xilinx-dataflow-lawyer %T/test.cpp -- -fhls -fstrict-dataflow 2>&1 | FileCheck %s --check-prefix=CHECK-MESSAGES
// RUN: FileCheck --check-prefix=CHECK-FIXES -input-file=%T/test.cpp %s

#define M 10

void mux(int *m, int *t) {
  *t = (*m) * (*m);
}
void divide(int *m, int *n, int *t) {
  *t = (*m + *n) / 81;
}

void sub1(int a[M][M], int b[M][M]) {
  for (int i = 0; i < M; i++) {
    for (int j = 0; j < M; j++) {
#pragma HLS dataflow
      //CHECK-FIXES: #pragma HLS dataflow
      //CHECK-MESSAGES: Since the only kind of statements allowed in a canonical dataflow region are variable declarations and function calls, the compiler may not be able to correctly handle the region
      b[i][j] = 3 * a[i][j];
    }
  }
}

void sub2(int a[M][M], int b[M][M], int c[M][M]) {
  int i, j;
  for (i = 0; i < M; i++) {
    for (j = 0; j < M; j++) {
#pragma HLS dataflow
      //CHECK-FIXES: #pragma HLS dataflow
      //CHECK-MESSAGES: Since the loop counter does not start from 0, the compiler may not successfully process the dataflow loop
      divide(&a[i][j], &b[i][j], &c[i][j]);
    }
  }
}

int dut(int a[M][M], int b[M][M], int e[M][M]) {
#pragma HLS dataflow
  //CHECK-FIXES: #pragma HLS dataflow
  //CHECK-MESSAGES: Since the only kind of statements allowed in a canonical dataflow region are variable declarations and function calls, the compiler may not be able to correctly handle the region
  int s[M][M], t[M][M];
  sub1(a, s);
  sub2(s, b, t);

  for (int i = 0; i < M; i++) {
    for (int j = 0; j < M; j++) {
#pragma HLS dataflow
      //CHECK-FIXES: #pragma HLS dataflow
      mux(&a[i][j], &e[i][j]);
      divide(&a[i][j], &b[i][j], &e[i][j]);
    }
  }
  return 0;
}
