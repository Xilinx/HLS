// Copyright (C) 2020, Silexica GmbH, Lichtstr. 25, Cologne, Germany
// All rights reserved

#include "botda.hpp"

// linearSVR() implements a Linear Support Vector Regression Decision Function
// as presented in Algorithm 1 of:
// Wu, Huan & Wang, Hongda & Choy, Chiu & Shu, Chester & Lu, Chao. (2018).
// BOTDA Fiber Sensor System Based on FPGA Accelerated Support Vector Regression.
// https://arxiv.org/pdf/1901.01141.pdf
float linearSVR(const float feature[M], const float support_vector[Ns][M],
            const float multipliers[Ns], float bias) {
  float partial_sum[Ns];
  loop_init: for (unsigned i = 0; i < Ns; ++i)
    partial_sum[i] = 0;
  float sum = bias;

  L1: for (int i = 0; i < Ns; ++i) {
  L2:   for (int j = 0; j < M ; ++j) {
          _SLXLoopInterchange();
          float square = support_vector[i][j] * feature[j];
          partial_sum[i] = partial_sum[i] + square;
        }

        float temp = multipliers[i] * partial_sum[i];
        sum = sum + temp;
      }

  return sum;
}

