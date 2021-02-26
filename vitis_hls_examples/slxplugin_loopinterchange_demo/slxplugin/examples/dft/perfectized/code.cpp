// From Ryan Kastner, Janarbek Matai, & Stephen Neuendorffer. (2018).
// Parallel Programming for FPGAs.
// https://arxiv.org/pdf/1805.03648.pdf pp 93
//
// Adapted by Silexica GmbH, Lichtstr. 25, Cologne, Germany. (2020).
//
// This work is licensed under the Creative Commons Attribution 4.0 International License

#include "code.h"
#include <math.h> //Required for cos and sin functions

// Figure 4.15: Baseline code for the DFT
// Change:
// - loops are named
// - loop perfectization has modified the statements S1, S2, and S3
void dft(IN_TYPE sample_real[N], IN_TYPE sample_imag[N]) {
  int i, j;
  TEMP_TYPE w;
  TEMP_TYPE c, s;
  // Temporary arrays to hold the intermediate frequency domain results
  TEMP_TYPE temp_real[N];
  TEMP_TYPE temp_imag[N];
  // Calculate each frequency domain sample_iteratively
  loop_freq:for (i = 0; i < N; i += 1) {
    // S1: temp_real[i] = 0;
    // S2: temp_imag[i] = 0;
    // (2 * pi * i)/N
    // S3: w = (2.0 * 3.141592653589 / N) * (TEMP_TYPE)i;
    // Calculate the jth frequency sample_sequentially
    loop_calc:for (j = 0; j < N; j += 1) {
      _SLXLoopInterchange();
      w = (2.0 * 3.141592653589 / N) * (TEMP_TYPE)i;  // S3'
      // Utilize HLS tool to calculate sine and cosine values
      c = cos(j * w);
      s = sin(j * w);
      // Multiply the current phasor with the appropriate input sample_and keep
      // running sum
      TEMP_TYPE prev_real = j ? temp_real[i] : 0;  // S1'
      TEMP_TYPE prev_imag = j ? temp_imag[i] : 0;  // S2'
      temp_real[i] = prev_real + (sample_real[j] * c - sample_imag[j] * s);
      temp_imag[i] = prev_imag + (sample_real[j] * s + sample_imag[j] * c);
    }
  }
  // Perform an inplace DFT, i.e., copy result into the input arrays
  loop_out:for (i = 0; i < N; i += 1) {
    sample_real[i] = temp_real[i];
    sample_imag[i] = temp_imag[i];
  }
}
