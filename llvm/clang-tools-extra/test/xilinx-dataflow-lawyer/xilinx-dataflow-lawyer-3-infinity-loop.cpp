// RUN: cp -f %s   %T/test3.cpp
// RUN: xilinx-dataflow-lawyer %T/test3.cpp -- -fhls -fstrict-dataflow 2>&1

// this case just check the infinity loop bug.

struct bintParam_str {
 int nr;
 short nc;
 char blkSz;
};

void sub(char);

__attribute__((sdx_kernel("cti0", 0))) void test(int idx)
{
#pragma HLS TOP name=test
#pragma HLS DATAFLOW


 const bintParam_str bintParam[] = {
{2025, 8, 3},
{1350, 12, 1},
{2315, 7, 1},
{2025, 8, 3},
{1800, 9, 2},
{810, 20, 1},
{1473, 11, 1},
{675, 24, 1},
{675, 24, 1},
{0, 24, 1}
 };


 sub(bintParam[idx].blkSz);
}
