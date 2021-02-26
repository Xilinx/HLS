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
static int check_result(size_t cnt, IN_TYPE *real, IN_TYPE *imag) {
  size_t errors = 0;
  size_t result_golden_cnt =
      sizeof(result_golden) / sizeof(result_golden[0]) / 2;
  if (cnt != result_golden_cnt) {
    std::cerr << "expected " << result_golden_cnt << " results, got " << cnt
              << std::endl;
    errors++;
  }
  for (size_t i = 0; i < cnt && i < result_golden_cnt; ++i) {
    if (!almost_equal(real[i], result_golden[2 * i])) {
      std::cerr << "result " << i << " real: expected " << result_golden[2 * i]
                << ", got " << real[i] << std::endl;
      errors++;
    }
    if (!almost_equal(imag[i], result_golden[2 * i + 1])) {
      std::cerr << "result " << i << " imag: expected "
                << result_golden[2 * i + 1] << ", got " << imag[i] << std::endl;
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
  IN_TYPE sample_real[N];
  IN_TYPE sample_imag[N];

  int i, j;

  IN_TYPE sample_r = 100.0;
  IN_TYPE sample_i = 100.0;
  for (i = 0; i < N; ++i) {
    sample_real[i] = sample_r;
    sample_imag[i] = sample_i;
    std::cout << "sample in is " << sample_real[i] << " and " << sample_imag[i]
              << " to " << i << "!!\n";
    sample_r = sample_r + 1;
    sample_i = sample_i - 1;
  }

  // Call the function
  dft(sample_real, sample_imag);
  for (i = 0; i < N; ++i) {
    std::cout << sample_real[i] << "r " << std::endl;
    std::cout << sample_imag[i] << "i " << std::endl;
  }

  // Check the results
  int retval = check_result(N, sample_real, sample_imag);

  return retval;
}
