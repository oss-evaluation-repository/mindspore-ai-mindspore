/**
 * Copyright 2020 Huawei Technologies Co., Ltd
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "nnacl/fp16/crop_fp16.h"

#include <string.h>

#include "nnacl/crop_parameter.h"

void Fp16Crop1D(const float16_t *input, float16_t *output, int *out_shape, int64_t *in_offset, int task_id,
                int thread_count) {
  const int out_batch = out_shape[0];
  int64_t task_id_stride = thread_count > 1 ? UP_DIV(out_batch, thread_count) : out_batch;
  if (task_id_stride <= 0) {
    return;
  }
  int n = task_id * task_id_stride;
  if (n >= out_batch) {
    return;
  }
  const float16_t *in_ptr = input + n + in_offset[0];
  float16_t *out_ptr = output + n;
  int64_t out_dist_stride = MSMIN(out_batch - task_id * task_id_stride, task_id_stride);
  memcpy(out_ptr, in_ptr, sizeof(float16_t) * out_dist_stride);
}

void Fp16Crop2D(const float16_t *input, float16_t *output, int *in_shape, int *out_shape, int64_t *in_offset,
                int task_id, int thread_count) {
  const int in_height = in_shape[1];
  const int out_batch = out_shape[0];
  const int out_height = out_shape[1];

  int64_t task_id_stride = thread_count > 1 ? UP_DIV(out_height, thread_count) : out_height;
  if (task_id_stride <= 0) {
    return;
  }

  for (int n = 0; n < out_batch; n++) {
    int h = task_id * task_id_stride;
    if (h >= out_height) {
      return;
    }
    const float16_t *in_ptr = input + (n + in_offset[0]) * in_height + h + in_offset[1];
    float16_t *out_ptr = output + n * out_height + h;
    int64_t out_dist_stride = MSMIN(out_height - task_id * task_id_stride, task_id_stride);
    memcpy(out_ptr, in_ptr, sizeof(float16_t) * out_dist_stride);
  }
}

void Fp16Crop3D(const float16_t *input, float16_t *output, int *in_shape, int *out_shape, int64_t *in_offset,
                int task_id, int thread_count) {
  const int in_height = in_shape[1];
  const int in_width = in_shape[2];

  const int out_batch = out_shape[0];
  const int out_height = out_shape[1];
  const int out_width = out_shape[2];

  int64_t task_id_stride = thread_count > 1 ? UP_DIV(out_height, thread_count) : out_height;
  if (task_id_stride <= 0) {
    return;
  }

  const int in_stride_h = in_width;
  const int in_stride_n = in_stride_h * in_height;

  const int out_stride_h = out_width;
  const int out_stride_n = out_stride_h * out_height;

  for (int n = 0; n < out_batch; n++) {
    for (int t = 0; t < task_id_stride; t++) {
      int h = t + task_id * task_id_stride;
      if (h >= out_height) {
        break;
      }
      const float16_t *in_ptr =
        input + (n + in_offset[0]) * in_stride_n + (h + in_offset[1]) * in_stride_h + in_offset[2];
      float16_t *out_ptr = output + n * out_stride_n + h * out_stride_h;
      memcpy(out_ptr, in_ptr, sizeof(float16_t) * out_width);
    }
  }
}

void Fp16Crop4D(const float16_t *input, float16_t *output, int *in_shape, int *out_shape, int64_t *in_offset,
                int task_id, int thread_count) {
  const int in_height = in_shape[1];
  const int in_width = in_shape[2];
  const int in_channel = in_shape[3];

  const int out_batch = out_shape[0];
  const int out_height = out_shape[1];
  const int out_width = out_shape[2];
  const int out_channel = out_shape[3];

  int64_t task_id_stride = thread_count > 1 ? UP_DIV(out_height, thread_count) : out_height;
  if (task_id_stride <= 0) {
    return;
  }

  const int in_stride_w = in_channel;
  const int in_stride_h = in_channel * in_width;
  const int in_stride_n = in_stride_h * in_height;

  const int out_stride_w = out_channel;
  const int out_stride_h = out_channel * out_width;
  const int out_stride_n = out_stride_h * out_height;

  for (int n = 0; n < out_batch; n++) {
    for (int t = 0; t < task_id_stride; t++) {
      int h = t + task_id * task_id_stride;
      if (h >= out_height) {
        break;
      }
      for (int w = 0; w < out_width; w++) {
        const float16_t *in_ptr = input + (n + in_offset[0]) * in_stride_n + (h + in_offset[1]) * in_stride_h +
                                  (w + in_offset[2]) * in_stride_w + in_offset[3];
        float16_t *out_ptr = output + n * out_stride_n + h * out_stride_h + w * out_stride_w;
        memcpy(out_ptr, in_ptr, sizeof(float16_t) * out_channel);
      }
    }
  }
}

void Fp16Crop(const float16_t *input, float16_t *output, int *in_shape, int *out_shape, int64_t *in_offset,
              int input_dim, int task_id, int thread_num) {
  switch (input_dim) {
    case 1:
      Fp16Crop1D(input, output, out_shape, in_offset, task_id, thread_num);
      break;
    case 2:
      Fp16Crop2D(input, output, in_shape, out_shape, in_offset, task_id, thread_num);
      break;
    case 3:
      Fp16Crop3D(input, output, in_shape, out_shape, in_offset, task_id, thread_num);
      break;
    case 4:
      Fp16Crop4D(input, output, in_shape, out_shape, in_offset, task_id, thread_num);
      break;
    default:
      break;
  }
}
