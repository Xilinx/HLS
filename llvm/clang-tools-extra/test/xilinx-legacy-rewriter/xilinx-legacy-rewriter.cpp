// RUN: cp %s   %T/test.cpp
// RUN: xilinx-legacy-rewriter %T/test.cpp --
// RUN: FileCheck %s -input-file=%T/test.cpp

typedef struct { int i; } A;
A c;
class sample
//struct sample
{
    int array1[64], array2[64];
    A a;
    sample()
    {
     #pragma AUTOPILOT array variable=array1,*,"arr",array2 partition complete
// CHECK: #pragma AUTOPILOT array variable=&array1,*,"arr",&array2 partition complete
 }

};

void if0(A a) {
int b;
A *e;
#pragma HLS INTERFACE s_axilite port=a,e bundle=control
// CHECK: #pragma HLS INTERFACE s_axilite port=&a,&e bundle=control
}

