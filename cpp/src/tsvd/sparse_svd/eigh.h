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

#pragma once

#include "qr.h"
#include <linalg/cublas_wrappers.h>
#include <linalg/cusolver_wrappers.h>
#include <linalg/eig.h>
#include <linalg/unary_op.h>
#include <common/device_buffer.hpp>
#include "common/cumlHandle.hpp"
#include <matrix/matrix.h>

#define device_buffer    MLCommon::device_buffer

namespace ML {


template <typename math_t>
int prepare_eigh(math_t *__restrict W,
                 math_t *__restrict V,
                 const int p,
                 const cumlHandle &handle)
{
  const cusolverDnHandle_t solver_h = handle.getImpl().getcusolverDnHandle();
  int lwork = 0;

  CUSOLVER_CHECK(MLCommon::LinAlg::cusolverDnsyevd_bufferSize(solver_h,
                 CUSOLVER_EIG_MODE_VECTOR, CUBLAS_FILL_MODE_UPPER, p,
                 &V[0], p, &W[0], &lwork));
  return lwork;
}



template <typename math_t>
void eigh(math_t *__restrict W,
          math_t *__restrict V,
          const int p,
          const int k,
          const cumlHandle &handle,
          const bool singular_values = false,
          int lwork = 0,
          math_t *__restrict work = NULL,
          int *__restrict info = NULL)
{
  auto d_alloc = handle.getDeviceAllocator();
  const cudaStream_t stream = handle.getStream();
  const cusolverDnHandle_t solver_h = handle.getImpl().getcusolverDnHandle();

  // Only allocate workspace if lwork or work is NULL
  device_buffer<math_t> work_(d_alloc, stream);
  if (work == NULL) {
    lwork = prepare_eigh(W, V, p, handle);
    work_.resize(lwork, stream);
    work = work_.data();
  }

  device_buffer<int> info_(d_alloc, stream);
  if (info == NULL) {
    info_.resize(1, stream);
    info = info_.data(); 
  }

  // Divide n Conquer Eigendecomposition
  CUSOLVER_CHECK(MLCommon::LinAlg::cusolverDnsyevd(solver_h,
                 CUSOLVER_EIG_MODE_VECTOR, CUBLAS_FILL_MODE_UPPER, p, &V[0],
                 p, &W[0], &work[0], lwork, &info[0], stream));

  // Reverse W, V since syevd provides smallest eigenvalues first
  MLCommon::Matrix::colReverse(&W[0], 1, p, stream);
  MLCommon::Matrix::colReverse(&V[0], p, p, stream);

  if (singular_values) {
    // Square root
    MLCommon::LinAlg::unaryOp(W, W, k,
      [] __device__(math_t x) { return ((x > 0) ? MLCommon::mySqrt(x) : 0); }, stream);
    CUDA_CHECK(cudaPeekAtLastError());
  }
}

} // namespace ML

#undef device_buffer