/* Copyright (C) 2020, Silexica GmbH, Lichtstr. 25, Cologne, Germany
 * All rights reserved
 */

#include "code.h"
#include "result_golden.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

static bool almost_equal(double a, double b) {
  if (a == 0) {
    return a == b;
  }
  // check to about 5 significant digits
  return fabs(a - b) / fabs(a) < 1e-5;
}

// check result and return exit code
static int check_result(size_t cnt, IN_TYPE *values) {
  size_t errors = 0;
  size_t result_golden_cnt = sizeof(result_golden) / sizeof(result_golden[0]);
  if (cnt != result_golden_cnt) {
    std::cerr << "expected " << result_golden_cnt << " results, got "
              << cnt << std::endl;
    errors++;
  }
  for (size_t i = 0; i < cnt && i < result_golden_cnt; ++i) {
    if (! almost_equal(values[i], result_golden[i]))  {
      std::cerr << "result " << i << ": expected " << result_golden[i]
                << ", got " << values[i] << std::endl;
      errors++;
    }
  }
  if (errors > 0) {
    std::cerr << "check failed, errors: " << errors << std::endl;
    return EXIT_FAILURE;
  }
  std::cerr << "check successful" << std::endl;
  return EXIT_SUCCESS;
}

int main() {
  IN_TYPE sample[N][N];
  IN_TYPE acc[N];

  int i, j;

  IN_TYPE sample_r = 100.0;
  for (i = 0; i < N; ++i) {
    acc[i] = i;
    for (j = 0; j < N; ++j) {
      sample[i][j] = sample_r;
      sample_r = sample_r + 1;
    }
  }

  // Call the function
  reduce_2d_to_1d(sample, acc);
  for (i = 0; i < N; ++i) {
    std::cout << acc[i] << std::endl;
  }

  // Check the results
  int retval = check_result(N, acc);

  return retval;
}
