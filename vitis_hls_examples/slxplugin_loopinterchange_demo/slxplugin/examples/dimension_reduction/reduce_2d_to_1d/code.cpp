// Copyright (C) 2020, Silexica GmbH, Lichtstr. 25, Cologne, Germany
// All rights reserved

#include "code.h"

// Dimensionality reduction combines information from multiple
// dimensions to a smaller number, typically as a lossy operation
// resulting in denser information. The many applicable areas
// include signal processing and feature extraction.
//
// This function is a simple variant, summing each row of
// `data` into a single element in `acc`.
void reduce_2d_to_1d(IN_TYPE data[N][N], OUT_TYPE acc[N]) {
  loop_out:for (int i = 0; i < N; i += 1) {
    loop_in:for (int j = 0; j < N; j += 1) {
      _SLXLoopInterchange();
      acc[i] = acc[i] + data[i][j];
    }
  }
}
