// Copyright (C) 2020, Silexica GmbH, Lichtstr. 25, Cologne, Germany
// All rights reserved

#include "botda.hpp"
#include <stdio.h>

// Include pre-computed support-vectors and weights/multipliers.
#include "botda_supportVector.hpp"
#include "botda_multipliers.hpp"

// Include a test feature vector and the expected result.
#include "botda_testFeature.hpp"

int main() {
  float res1 = linearSVR(testFeature, supportVector, multipliers, testBias);

  // Compare the results with a ref
  if (expectedResultForTestFeature != res1) {
    printf("Test failed %f %f !!!\n", expectedResultForTestFeature, res1);
    return 1;
  } else {
    printf("Test passed !\n");
  }

  // Return 0 if the test passed
  return 0;
}
