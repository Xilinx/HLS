// Copyright (C) 2020, Silexica GmbH, Lichtstr. 25, Cologne, Germany
// All rights reserved

#include "botda.hpp"

// linearSVR() implements a Linear Support Vector Regression Decision Function
// as presented in Algoritm 1 of:
// Wu, Huan & Wang, Hongda & Choy, Chiu & Shu, Chester & Lu, Chao. (2018).
// BOTDA Fiber Sensor System Based on FPGA Accelerated Support Vector Regression.
// https://arxiv.org/pdf/1901.01141.pdf
float linearSVR(const float feature[M], const float support_vector[Ns][M],
            const float multipliers[Ns], float bias) {
  float partial_sum[Ns];
  loop_init: for (unsigned i = 0; i < Ns; ++i)
    partial_sum[i] = 0;
  float sum = bias;

  // Modification from Algorithm 1: L3 is split out of L1,
  // as the authors also do in Algorithm 2.
  // Looking at this from a SW developer perspective the split
  // looks counter intuitive. Yet, it leads to a better performance
  // after interchanging L1 and L2, as demonstrated in latency.ref
  // and discussed in the referenced article:
  // From an HLS developer perspective, the split makes sense because
  // the operations in [L1-L2] happen Ns*M times and dominate the
  // hotspot (compared to what is left in L3 that only happens Ns times).
  // Since the interchange relocates the LCD and allows achieving and
  // II=1 (instead of II=9) for [L1-L2], a huge gain is to be expected
  // (theoretically ~9x, iff Ns*M >> Ns). As a matter of fact, we
  // achieve ~8.5x gains with the interchange, even if L3 is left as an
  // individual loop with an II=9.
  L1: for (int i = 0; i < Ns; ++i) {
  L2:   for (int j = 0; j < M ; ++j) {
          _SLXLoopInterchange();
          float square = support_vector[i][j] * feature[j];
          partial_sum[i] = partial_sum[i] + square;
        }
      }

  L3: for (int i = 0; i < Ns; ++i) {
      float temp = multipliers[i] * partial_sum[i];
      sum = sum + temp;
  }

  return sum;
}

