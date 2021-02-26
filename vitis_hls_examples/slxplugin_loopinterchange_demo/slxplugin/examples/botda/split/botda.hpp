// Copyright (C) 2020, Silexica GmbH, Lichtstr. 25, Cologne, Germany
// All rights reserved

#ifndef BOTDA_H
#define BOTDA_H

constexpr int Ns = 1136;
constexpr int M = 220;

float linearSVR(const float feature[M], const float support_vector[Ns][M],
            const float multipliers[Ns], float bias);

#endif /* !BOTDA_H */
