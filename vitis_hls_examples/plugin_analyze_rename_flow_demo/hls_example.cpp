// (C) Copyright 2016-2021 Xilinx, Inc.
// All Rights Reserved.
//
// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
#include <iostream>

constexpr size_t N = 50;

using namespace std;

void example(int a[N], int b[N]) {
#pragma HLS INTERFACE m_axi port = a depth = N
#pragma HLS INTERFACE m_axi port = b depth = N
  int buff[N];
  for (size_t i = 0; i < N; ++i) {
#pragma HLS PIPELINE II = 1
    buff[i] = a[i];
    buff[i] = buff[i] + 100;
    b[i] = buff[i];
  }
}

int main() {
  int in[N], res[N];
  for (size_t i = 0; i < N; ++i) {
    in[i] = i;
  }

  example(in, res);

  for (int i = 0; i < N; ++i)
    if (res[i] != i + 100) {
      cout << "Test failed.\n";
      return 1;
    }

  cout << "Test passed.\n";
  return 0;
}
