// Copyright (C) 2020, Silexica GmbH, Lichtstr. 25, Cologne, Germany
// All rights reserved
//
// Implemented by Silexica based on algorithm 3 in:
//
// Stone, S. S., Haldar, J. P., Tsao, S. C., Hwu, W. M., Sutton, B. P.,
// & Liang, Z. P. (2008). Accelerating Advanced MRI Reconstructions on GPUs.
// Journal of parallel and distributed computing, 68(10), 1307–1318.
// https://doi.org/10.1016/j.jpdc.2008.05.013

#include "mri_vhls.h"
#include <math.h>

// C code for the algorithm that compute Q - the
// first step of the advanced MRI reconstruction algorithm.
void mri_fk(float kphi_r[M], float kphi_i[M],
            float kx[M],     float ky[M],     float kz[M],
            float xx[8 * N], float xy[8 * N], float xz[8 * N],
            float rQ[8 * N], float iQ[8 * N]) {

  float PI_x_2 = (3.14 * 2.0);
  float kphi_mag[M];
  float exponent;

LOOP_MAGPHI:
  for (int m = 0; m < M; ++m) {
    kphi_mag[m] = kphi_r[m] * kphi_r[m] + kphi_i[m] * kphi_i[m];
  }

LOOP_N:
  for (int n = 0; n < 8 * N; ++n) {
  LOOP_M:
    for (int m = 0; m < M; ++m) {
_SLXLoopInterchange();
      exponent = PI_x_2 * (kx[m] * xx[n] + ky[m] * xy[n] + kz[m] * xz[n]);
      rQ[n] += kphi_mag[m] * cosf(exponent);
      iQ[n] += kphi_mag[m] * sinf(exponent);
    }
  }
}
