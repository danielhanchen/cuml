/*
 * Copyright (c) 2019, NVIDIA CORPORATION.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <cuml/decomposition/sparse_svd.h>
#include "../../src_prims/utils.h"

namespace ML {

template <typename math_t>
void SparseSVD(const cumlHandle &handle,
               const math_t *__restrict X,
               const int n,
               const int p,
               math_t *__restrict U,
               math_t *__restrict S,
               math_t *__restrict VT,
               const int n_components = 2,
               const int n_oversamples = 10,
               const int max_iter = 3)
{
  printf("Hi!\n");
}



}  // namespace ML
