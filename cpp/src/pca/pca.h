/*
 * Copyright (c) 2018, NVIDIA CORPORATION.
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

#include <cuda_utils.h>
#include <linalg/cublas_wrappers.h>
#include <linalg/eig.h>
#include <linalg/eltwise.h>
#include <linalg/transpose.h>
#include <matrix/math.h>
#include <matrix/matrix.h>
#include <stats/cov.h>
#include <stats/mean.h>
#include <stats/mean_center.h>
#include "common/cumlHandle.hpp"
#include "common/device_buffer.hpp"
#include "cuML.hpp"
#include "ml_utils.h"
#include "tsvd/tsvd.h"

namespace ML {

using namespace MLCommon;

template <typename math_t>
void truncCompExpVars(const cumlHandle_impl &handle, math_t *in,
                      math_t *components, math_t *explained_var,
                      math_t *explained_var_ratio, paramsTSVD prms,
                      cudaStream_t stream) {
  int len = prms.n_cols * prms.n_cols;
  auto allocator = handle.getDeviceAllocator();
  device_buffer<math_t> components_all(allocator, stream, len);
  device_buffer<math_t> explained_var_all(allocator, stream, prms.n_cols);
  device_buffer<math_t> explained_var_ratio_all(allocator, stream, prms.n_cols);

  calEig(handle, in, components_all.data(), explained_var_all.data(), prms,
         stream);
  Matrix::truncZeroOrigin(components_all.data(), prms.n_cols, components,
                          prms.n_components, prms.n_cols, stream);
  Matrix::ratio(explained_var_all.data(), explained_var_ratio_all.data(),
                prms.n_cols, allocator, stream);
  Matrix::truncZeroOrigin(explained_var_all.data(), prms.n_cols, explained_var,
                          prms.n_components, 1, stream);
  Matrix::truncZeroOrigin(explained_var_ratio_all.data(), prms.n_cols,
                          explained_var_ratio, prms.n_components, 1, stream);
}

/**
 * @brief perform fit operation for the pca. Generates eigenvectors, explained vars, singular vals, etc.
 * @input param handle: cuml handle object
 * @input param input: the data is fitted to PCA. Size n_rows x n_cols. The size of the data is indicated in prms.
 * @output param components: the principal components of the input data. Size n_cols * n_components.
 * @output param explained_var: explained variances (eigenvalues) of the principal components. Size n_components * 1.
 * @output param explained_var_ratio: the ratio of the explained variance and total variance. Size n_components * 1.
 * @output param singular_vals: singular values of the data. Size n_components * 1
 * @output param mu: mean of all the features (all the columns in the data). Size n_cols * 1.
 * @output param noise_vars: variance of the noise. Size 1 * 1 (scalar).
 * @input param prms: data structure that includes all the parameters from input size to algorithm.
 * @input param stream cuda stream
 */
template <typename math_t>
void pcaFit(const cumlHandle_impl &handle, math_t *input, math_t *components,
            math_t *explained_var, math_t *explained_var_ratio,
            math_t *singular_vals, math_t *mu, math_t *noise_vars,
            paramsPCA prms, cudaStream_t stream) {
  auto cublas_handle = handle.getCublasHandle();

  ASSERT(prms.n_cols > 1,
         "Parameter n_cols: number of columns cannot be less than two");
  ASSERT(prms.n_rows > 1,
         "Parameter n_rows: number of rows cannot be less than two");
  ASSERT(
    prms.n_components > 0,
    "Parameter n_components: number of components cannot be less than one");

  if (prms.n_components > prms.n_cols) prms.n_components = prms.n_cols;

  Stats::mean(mu, input, prms.n_cols, prms.n_rows, true, false, stream);

  // Also create sum array
  device_buffer<math_t> sum(handle.getDeviceAllocator(), stream, prms.n_cols);

  if ((sizeof(math_t) == sizeof(float)) or (sizeof(math_t) == sizeof(double))) {
    // Fast path
    // Sum is just n * mean
    // This only executes for float32, float64 data
    thrust::copy(thrust::cuda::par.on(stream), mu, mu + prms.n_cols,
                 sum.data());
    MLCommon::LinAlg::scalarMultiply(sum.data(), sum.data(),
                                     (math_t)prms.n_rows, prms.n_cols, stream);
  }

  int len = prms.n_cols * prms.n_cols;
  device_buffer<math_t> cov(handle.getDeviceAllocator(), stream, len);

  Stats::cov(cov.data(), input, mu, sum.data(), prms.n_cols, prms.n_rows, true,
             false, true, cublas_handle, stream);
  truncCompExpVars(handle, cov.data(), components, explained_var,
                   explained_var_ratio, prms, stream);

  math_t scalar = (prms.n_rows - 1);
  Matrix::seqRoot(explained_var, singular_vals, scalar, prms.n_components,
                  stream, true);

  // If does not use fast ssyrk + mean trick path
  if ((sizeof(math_t) != sizeof(float)) and
      (sizeof(math_t) != sizeof(double))) {
    // Column wise add mean
    Stats::meanAdd(input, input, mu, prms.n_cols, prms.n_rows, false, true,
                   stream);
  }
}

/**
 * @brief perform fit and transform operations for the pca. Generates transformed data, eigenvectors, explained vars, singular vals, etc.
 * @input param handle: cuml handle object
 * @input param input: the data is fitted to PCA. Size n_rows x n_cols. The size of the data is indicated in prms.
 * @output param trans_input: the transformed data. Size n_rows * n_components.
 * @output param components: the principal components of the input data. Size n_cols * n_components.
 * @output param explained_var: explained variances (eigenvalues) of the principal components. Size n_components * 1.
 * @output param explained_var_ratio: the ratio of the explained variance and total variance. Size n_components * 1.
 * @output param singular_vals: singular values of the data. Size n_components * 1
 * @output param mu: mean of all the features (all the columns in the data). Size n_cols * 1.
 * @output param noise_vars: variance of the noise. Size 1 * 1 (scalar).
 * @input param prms: data structure that includes all the parameters from input size to algorithm.
 * @input param stream cuda stream
 */
template <typename math_t>
void pcaFitTransform(const cumlHandle_impl &handle, math_t *input,
                     math_t *trans_input, math_t *components,
                     math_t *explained_var, math_t *explained_var_ratio,
                     math_t *singular_vals, math_t *mu, math_t *noise_vars,
                     paramsPCA prms, cudaStream_t stream) {
  pcaFit(handle, input, components, explained_var, explained_var_ratio,
         singular_vals, mu, noise_vars, prms, stream);
  pcaTransform(handle, input, components, trans_input, singular_vals, mu, prms,
               stream);
  signFlip(trans_input, prms.n_rows, prms.n_components, components, prms.n_cols,
           handle.getDeviceAllocator(), stream);
}

// TODO: implement pcaGetCovariance function
template <typename math_t>
void pcaGetCovariance() {
  ASSERT(false, "pcaGetCovariance: will be implemented!");
}

// TODO: implement pcaGetPrecision function
template <typename math_t>
void pcaGetPrecision() {
  ASSERT(false, "pcaGetPrecision: will be implemented!");
}

/**
 * @brief performs inverse transform operation for the pca. Transforms the transformed data back to original data.
 * @input param handle: the internal cuml handle object
 * @input param trans_input: the data is fitted to PCA. Size n_rows x n_components.
 * @input param components: transpose of the principal components of the input data. Size n_components * n_cols.
 * @input param singular_vals: singular values of the data. Size n_components * 1
 * @input param mu: mean of features (every column).
 * @output param input: the data is fitted to PCA. Size n_rows x n_cols.
 * @input param prms: data structure that includes all the parameters from input size to algorithm.
 * @input param stream cuda stream
 */
template <typename math_t>
void pcaInverseTransform(const cumlHandle_impl &handle, math_t *trans_input,
                         math_t *components, math_t *singular_vals, math_t *mu,
                         math_t *input, paramsPCA prms, cudaStream_t stream) {
  ASSERT(prms.n_cols > 1,
         "Parameter n_cols: number of columns cannot be less than two");
  ASSERT(prms.n_rows > 1,
         "Parameter n_rows: number of rows cannot be less than two");
  ASSERT(
    prms.n_components > 0,
    "Parameter n_components: number of components cannot be less than one");

  if (prms.whiten) {
    math_t scalar = math_t(1 / sqrt(prms.n_rows - 1));
    LinAlg::scalarMultiply(components, components, scalar,
                           prms.n_rows * prms.n_components, stream);
    Matrix::matrixVectorBinaryMultSkipZero(components, singular_vals,
                                           prms.n_rows, prms.n_components, true,
                                           true, stream);
  }

  tsvdInverseTransform(handle, trans_input, components, input, prms, stream);
  Stats::meanAdd(input, input, mu, prms.n_cols, prms.n_rows, false, true,
                 stream);

  if (prms.whiten) {
    Matrix::matrixVectorBinaryDivSkipZero(components, singular_vals,
                                          prms.n_rows, prms.n_components, true,
                                          true, stream);
    math_t scalar = math_t(sqrt(prms.n_rows - 1));
    LinAlg::scalarMultiply(components, components, scalar,
                           prms.n_rows * prms.n_components, stream);
  }
}

// TODO: implement pcaScore function
template <typename math_t>
void pcaScore() {
  ASSERT(false, "pcaScore: will be implemented!");
}

// TODO: implement pcaScoreSamples function
template <typename math_t>
void pcaScoreSamples() {
  ASSERT(false, "pcaScoreSamples: will be implemented!");
}

/**
 * @brief performs transform operation for the pca. Transforms the data to eigenspace.
 * @input param handle: the internal cuml handle object
 * @input param input: the data is transformed. Size n_rows x n_components.
 * @input param components: principal components of the input data. Size n_cols * n_components.
 * @output param trans_input:  the transformed data. Size n_rows * n_components.
 * @input param singular_vals: singular values of the data. Size n_components * 1.
 * @input param prms: data structure that includes all the parameters from input size to algorithm.
 * @input param stream cuda stream
 */
template <typename math_t>
void pcaTransform(const cumlHandle_impl &handle, math_t *input,
                  math_t *components, math_t *trans_input,
                  math_t *singular_vals, math_t *mu, paramsPCA prms,
                  cudaStream_t stream) {
  ASSERT(prms.n_cols > 1,
         "Parameter n_cols: number of columns cannot be less than two");
  ASSERT(prms.n_rows > 1,
         "Parameter n_rows: number of rows cannot be less than two");
  ASSERT(
    prms.n_components > 0,
    "Parameter n_components: number of components cannot be less than one");

  if (prms.whiten) {
    math_t scalar = math_t(sqrt(prms.n_rows - 1));
    LinAlg::scalarMultiply(components, components, scalar,
                           prms.n_rows * prms.n_components, stream);
    Matrix::matrixVectorBinaryDivSkipZero(components, singular_vals,
                                          prms.n_rows, prms.n_components, true,
                                          true, stream);
  }

  /*
  Stats::meanCenter(input, input, mu, prms.n_cols, prms.n_rows, false, true,
                    stream);
  tsvdTransform(handle, input, components, trans_input, prms, stream);
  Stats::meanAdd(input, input, mu, prms.n_cols, prms.n_rows, false, true,
                 stream);
  */

  // We want (X_new - mu) @ V = X_new @ V - mu @ V
  device_buffer<math_t> VT_mu(handle.getDeviceAllocator(), stream,
                              prms.n_components);

  // We need to minus mu @ V so alpha = -1
  const math_t alpha = -1, beta = 0;
  // components is C-Contiguous
  // TODO: Can store this internally as a buffer
  LinAlg::cublasgemv(handle.getCublasHandle(), CUBLAS_OP_N, prms.n_components,
                     prms.n_cols, &alpha, components, prms.n_components, mu, 1,
                     &beta, VT_mu.data(), 1, stream);

  tsvdTransform(handle, input, components, trans_input, prms, stream);

  // Add -mu @ V to X_new @ V
  Stats::meanAdd(trans_input, trans_input, VT_mu.data(), prms.n_components,
                 prms.n_rows, false, true, stream);

  if (prms.whiten) {
    Matrix::matrixVectorBinaryMultSkipZero(components, singular_vals,
                                           prms.n_rows, prms.n_components, true,
                                           true, stream);
    math_t scalar = math_t(1 / sqrt(prms.n_rows - 1));
    LinAlg::scalarMultiply(components, components, scalar,
                           prms.n_rows * prms.n_components, stream);
  }
}

};  // end namespace ML
