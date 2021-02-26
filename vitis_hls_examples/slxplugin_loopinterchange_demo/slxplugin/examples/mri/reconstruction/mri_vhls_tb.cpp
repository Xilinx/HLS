// Copyright (C) 2020, Silexica GmbH, Lichtstr. 25, Cologne, Germany
// All rights reserved

extern "C" {
#include "mri_vhls.h"
}
#include <cmath>
#include <iostream>
#include <fstream>
#include <stdio.h>
#include <stdlib.h>

// get the expected result - this should match the produced result.dat
#include "result.h"

using namespace std;

#define RAND_SCALE 100.0f

inline bool isEqual(float x, float y) {
  return std::abs(x - y) <= 0.01f * std::abs(x);
}

int main() {
  float kphi_r[M], kphi_i[M];
  float kx[M], ky[M], kz[M];
  float xx[8 * N], xy[8 * N], xz[8 * N];
  float rQ[8 * N], iQ[8 * N];

  // do not seed so rand delived the same data each test run.
  for (int i = 0; i < M; ++i) {
    kphi_r[i] = static_cast<float>(rand()) / static_cast<float>(RAND_SCALE);
    kphi_i[i] = static_cast<float>(rand()) / static_cast<float>(RAND_SCALE);
    kx[i] = static_cast<float>(rand()) / static_cast<float>(RAND_SCALE);
    ky[i] = static_cast<float>(rand()) / static_cast<float>(RAND_SCALE);
    kz[i] = static_cast<float>(rand()) / static_cast<float>(RAND_SCALE);
  }
  for (int k = 0; k < 8 * N; ++k) {
    xx[k] = static_cast<float>(rand()) / static_cast<float>(RAND_SCALE);
    xy[k] = static_cast<float>(rand()) / static_cast<float>(RAND_SCALE);
    xz[k] = static_cast<float>(rand()) / static_cast<float>(RAND_SCALE);
    rQ[k] = 0;
    iQ[k] = 0;
  }
  // Running the original function
  mri_fk(kphi_r, kphi_i, kx, ky, kz, xx, xy, xz, rQ, iQ);

  // Save the results to a file
  ofstream results;
  results.open("result.dat");
  results << "float rQ_ref[8 * N] = {";
  for (int z = 0; z < 8 * N; ++z) {
    if (z % 16 == 0)
      results << "\n  ";
    results << rQ[z] << ", ";
  }
  results << "};\n\n";
  results << "float iQ_ref[8 * N] = {";
  for (int z = 0; z < 8 * N; ++z) {
    if (z % 16 == 0)
      results << "\n  ";
    results << iQ[z] << ", ";
  }
  results << "};\n\n";
  results.close();

  // Check if the results are as expected
  for (int z = 0; z < 8 * N; ++z) {
    if (! isEqual(rQ[z], rQ_ref[z])) {
      cout << "Simulation Failed! rQ value: " << z << " not matching." << endl;
      return EXIT_FAILURE;
    }
  }
  for (int z = 0; z < 8 * N; ++z) {
    if (! isEqual(iQ[z], iQ_ref[z])) {
      cout << "Simulation Failed! iQ value: " << z << " not matching." << endl;
      return EXIT_FAILURE;
    }
  }

  cout << "Simulation Passed!" << endl;
  return 0;
}
