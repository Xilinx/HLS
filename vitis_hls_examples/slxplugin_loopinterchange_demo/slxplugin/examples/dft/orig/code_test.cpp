/*
 * Copyright 2019 Xilinx, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "code.h"
#include <cstdlib>
#include <fstream>
#include <iostream>

using namespace std;

int main() {
  IN_TYPE sample_real[N];
  IN_TYPE sample_imag[N];

  int i, j, retval = 0;
  ofstream FILE;

  IN_TYPE sample_r = 100.0;
  IN_TYPE sample_i = 100.0;
  for (i = 0; i < N; ++i) {
    sample_real[i] = sample_r;
    sample_imag[i] = sample_i;
    cout << "sample in is " << sample_real[i] << " and " << sample_imag[i]
         << " to " << i << "!!\n";
    sample_r = sample_r + 1;
    sample_i = sample_i - 1;
  }

  // Save the results to a file
  FILE.open("result.dat");

  // Call the function
  dft(sample_real, sample_imag);
  for (i = 0; i < N; ++i) {
    cout << sample_real[i] << "r " << endl;
    cout << sample_imag[i] << "i " << endl;
    FILE << sample_real[i] << endl;
    FILE << sample_imag[i] << endl;
  }
  FILE.close();

  // Compare the results file with the golden results
  retval = system("diff --brief -w result.dat result.golden.dat");
  // retval=0; // diff results each time, something unitialized or ??
  if (retval != 0) {
    cout << "Test failed  !!!\n";
    retval = 1;
  } else {
    cout << "Test passed !\n";
  }

  // Return 0 if the test passed
  return retval;
}
