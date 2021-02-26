// Copyright (C) 2020, Silexica GmbH, Lichtstr. 25, Cologne, Germany
// All rights reserved

#ifndef _CODE_H_
#define _CODE_H_

typedef double IN_TYPE;
typedef double OUT_TYPE;
#define N 256

extern "C" {
void reduce_2d_to_1d(IN_TYPE data[N][N], OUT_TYPE acc[N]);
}
#endif // _CODE_H_
