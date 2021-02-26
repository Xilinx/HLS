// From Ryan Kastner, Janarbek Matai, & Stephen Neuendorffer. (2018).
// Parallel Programming for FPGAs.
// https://arxiv.org/pdf/1805.03648.pdf pp 93
//
// Adapted by Silexica GmbH, Lichtstr. 25, Cologne, Germany. (2020).
//
// This work is licensed under the Creative Commons Attribution 4.0 International License

#ifndef _EX2_DFT_ORIG_H_
#define _EX2_DFT_ORIG_H_

typedef double IN_TYPE;   // Data type for the input signal
typedef double TEMP_TYPE; // Data type for the temporary variables
#define N 256             // DFT Size

extern "C" {
void dft(IN_TYPE sample_real[N], IN_TYPE sample_imag[N]);
}
#endif

