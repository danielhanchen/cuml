# Copyright (c) 2018-2019, NVIDIA CORPORATION.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import faulthandler
faulthandler.enable()

import pytest
from sklearn.manifold.t_sne import trustworthiness as sklearn_trustworthiness
from cuml.metrics import trustworthiness as cuml_trustworthiness

from sklearn.datasets.samples_generator import make_blobs
from umap import UMAP

import cudf
import numpy as np


@pytest.mark.parametrize('input_type', ['ndarray'])
@pytest.mark.parametrize('n_samples', [10, 100])
@pytest.mark.parametrize('n_features', [10, 100])
@pytest.mark.parametrize('n_components', [2, 8])
def test_trustworthiness(input_type, n_samples, n_features, n_components):
    centers = round(n_samples*0.4)
    print("Making blobs")
    X, y = make_blobs(n_samples=n_samples, centers=centers,
                      n_features=n_features)
    print("Blobs done")
    X_embedded = \
        UMAP(n_components=n_components).fit_transform(X)
    print("UMAP done")
    X = X.astype(np.float32)
    X_embedded = X_embedded.astype(np.float32)
    print("As type done")
    
    if input_type == 'dataframe':
        gdf = cudf.DataFrame()
        for i in range(X.shape[1]):
            gdf[str(i)] = np.asarray(X[:, i], dtype=np.float32)

        gdf_embedded = cudf.DataFrame()
        for i in range(X_embedded.shape[1]):
            gdf_embedded[str(i)] = np.asarray(X_embedded[:, i],
                                              dtype=np.float32)
        
        print("cudf done")
        score = cuml_trustworthiness(gdf, gdf_embedded)
    else:
        score = cuml_trustworthiness(X, X_embedded)
    
    print("cuml trust done")
    sk_score = sklearn_trustworthiness(X, X_embedded)
    print("sk trust done")

    eps = 0.001
    assert (sk_score * (1 - eps) <= score and
            score <= sk_score * (1 + eps))
    # assert cu_score == sk_score ideally
