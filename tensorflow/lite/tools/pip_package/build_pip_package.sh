#!/usr/bin/env bash
# Copyright 2018 The TensorFlow Authors. All Rights Reserved.
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
# ==============================================================================

set -e

PYTHON="${PYTHON:-python}"

# Find where this script lives and then the Tensorflow root.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

export TENSORFLOW_SRC_ROOT="${SCRIPT_DIR}/../../../.."
export TENSORFLOW_VERSION=`grep "_VERSION = " "${TENSORFLOW_SRC_ROOT}/tensorflow/tools/pip_package/setup.py" | cut -d'=' -f 2 | sed "s/[ '-]//g"`;

TFLITE_ROOT="${TENSORFLOW_SRC_ROOT}/tensorflow/lite"

# Build a pip build tree.
BUILD_ROOT="/tmp/tflite_pip/${PYTHON}"
rm -rf "${BUILD_ROOT}"
mkdir -p "${BUILD_ROOT}/tflite_runtime/"

# Copy necessary source files.
touch "${BUILD_ROOT}/tflite_runtime/__init__.py"
cp -r "${TFLITE_ROOT}/python/interpreter_wrapper" "${BUILD_ROOT}"
cp "${TFLITE_ROOT}/python/interpreter.py" "${BUILD_ROOT}/tflite_runtime/"
cp "${TFLITE_ROOT}/tools/pip_package/setup.py" "${BUILD_ROOT}"
cp "${TFLITE_ROOT}/tools/pip_package/MANIFEST.in" "${BUILD_ROOT}"

# Build wheel file.
cd "${BUILD_ROOT}"

if [[ "${TENSORFLOW_TARGET}" == "rpi" ]]; then
  ${PYTHON} setup.py bdist_wheel --plat-name=linux-armv7l
elif [[ "${TENSORFLOW_TARGET}" == "aarch64" ]]; then
  ${PYTHON} setup.py bdist_wheel --plat-name=linux-aarch64
else
  ${PYTHON} setup.py bdist_wheel
fi

