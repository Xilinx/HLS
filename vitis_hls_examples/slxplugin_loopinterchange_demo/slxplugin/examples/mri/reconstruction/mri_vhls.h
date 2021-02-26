// Copyright (C) 2020, Silexica GmbH, Lichtstr. 25, Cologne, Germany
// All rights reserved

#define M 1024
#define N 512

void mri_fk(float kphi_r[M], float kphi_i[M],
            float kx[M],     float ky[M],     float kz[M],
            float xx[8 * N], float xy[8 * N], float xz[8 * N],
            float rQ[8 * N], float iQ[8 * N]);
