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

#include <gtest/gtest.h>
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <vector>
#include "datasets/digits.h"
#include "linalg/mean_squared_error.h"
#include "tsvd/sparse_svd.cu"

#include "cuda_utils.h"

#include <cuml/common/cuml_allocator.hpp>
#include "common/device_buffer.hpp"

using namespace MLCommon;
using namespace MLCommon::Datasets::Digits;
using namespace ML;

#define printf(...) fprintf(stderr, __VA_ARGS__)

class SparseSVDTest : public ::testing::Test {
 protected:
  void basicTest()
  {
    cumlHandle handle;
    auto d_alloc = handle.getDeviceAllocator();
    const cudaStream_t stream = handle.getStream();
    const cublasHandle_t blas_h = handle.getImpl().getCublasHandle();

    const float *__restrict X = digits.data();
    device_buffer<float> X_(d_alloc, stream, n*p);


    // SparseSVD only accepts F-Contiguous data, but digits is C-Contiguous
    // Technically, no matter if the data is F or C contiguous, SparseSVD
    // should work regardless. The only difference is U, VT are swapped.
    float *__restrict XT = (float*) malloc(sizeof(float) * n * p);
    ASSERT(XT != NULL, "No more memory!");

    #define XT(i,j)   XT[(i) + (j)*n]
    #define X(i,j)    X[(i)*p + (j)]

    for (int i = 0; i < n; i++) {
      #pragma omp simd
      for (int j = 0; j < p; j++)
        XT(i, j) = X(i, j);
    }
    MLCommon::updateDevice(X_.data(), XT, n*p, stream);
    CUDA_CHECK(cudaStreamSynchronize(stream));


    // Allocate U, S, VT
    device_buffer<float> U_(d_alloc, stream, n*k);
    device_buffer<float> S_(d_alloc, stream, k);
    device_buffer<float> VT_(d_alloc, stream, k*p);

    SparseSVD(handle, X_.data(), n, p, U_.data(), S_.data(), VT_.data(), k,
              10, 3, -1, true);


    // Move U, S, VT to malloced space
    #define U(i,j)    U[(i) + (j)*n]
    #define S(i)      S[(i)]
    #define VT(i,j)   VT[(i) + (j)*p]
    float *__restrict U = (float*) malloc(sizeof(float) * n * k);
    float *__restrict S = (float*) malloc(sizeof(float) * k);
    float *__restrict VT = (float*) malloc(sizeof(float) * k * p);
    ASSERT(U != NULL and S != NULL and VT != NULL, "Out of memory!");

    MLCommon::updateHost(U, U_.data(), n*k, stream);
    MLCommon::updateHost(S, S_.data(), k, stream);
    MLCommon::updateHost(VT, VT_.data(), k*p, stream);
    CUDA_CHECK(cudaStreamSynchronize(stream));


    // Confirm singular values
    // Should be around {2193, 566, 542, 504, 425}
    n_correct = 0;
    for (int i = 0; i < k; i++) {
      if ((S(i) <= (compare_S[i] + 10)) and (S(i) >= (compare_S[i] - 10)))
        n_correct += 1;

      fprintf(stdout, "Singular Value[%d Correct = %.2f] = %.3f\n", i, compare_S[i], S(i));
    }

    // Compute X_hat = U * S @ VT
    device_buffer<float> X_hat_(d_alloc, stream, n*p);

    // U * S
    MLCommon::Matrix::matrixVectorBinaryMult(U_.data(), S_.data(), n, k, false, false, stream);

    // (U * S) @ VT
    MLCommon::LinAlg::gemm(U_.data(), n, k, VT_.data(), X_hat_.data(), n, p,
                           CUBLAS_OP_N, CUBLAS_OP_N, blas_h, stream);
    MLCommon::updateHost(U, U_.data(), n*k, stream);
    CUDA_CHECK(cudaStreamSynchronize(stream));

    for (int j = 0; j < k; j++) {
      for (int i = 0; i < 10; i++)
        printf("%.2f, ", U(i, j));
      printf("\n");
    }

    // Now check error
    // sum(square(X - X_hat))
    device_buffer<float> sse_(d_alloc, stream, 1);
    MLCommon::LinAlg::meanSquaredError(sse_.data(), X_.data(), X_hat_.data(), n*p, 1.0f, stream);
    float sse;
    MLCommon::updateHost(&sse, sse_.data(), 1, stream);
    CUDA_CHECK(cudaStreamSynchronize(stream));

    fprintf(stdout, "Sum of squared errors = %.3f\n", sse);

    free(U);
    free(S);
    free(VT);
    free(XT);
    #undef XT
    #undef X
    #undef U
    #undef S
    #undef VT
  }

  void SetUp() override { basicTest(); }

  void TearDown() override {}

 protected:
  const int n = 1797;
  const int p = 64;
  const int k = 5;
  const float compare_S[5] = {2193., 566., 542., 504., 425.};
  int n_correct;
  float sse;
};


typedef SparseSVDTest SparseSVDTestF;
TEST_F(SparseSVDTestF, Result)
{
  fprintf(stdout, "Percentage of good singular values = %.2f", (float)n_correct/(float)k);
  ASSERT(n_correct == k, "Singular values are off!");
}

#undef printf
