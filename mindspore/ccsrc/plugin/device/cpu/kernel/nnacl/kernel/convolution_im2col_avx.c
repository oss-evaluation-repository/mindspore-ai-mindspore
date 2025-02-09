/**
 * Copyright 2023 Huawei Technologies Co., Ltd
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either convolutionress or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifdef ENABLE_AVX
#include "nnacl/kernel/convolution_im2col_avx.h"
#include "nnacl/fp32/pack_fp32.h"
#include "nnacl/fp32/conv_common_fp32.h"

void ConvIm2ColAVXInitGlobalVariable(ConvolutionBaseStruct *conv) {
  ConvolutionIm2ColBaseStruct *conv_im2col = (ConvolutionIm2ColBaseStruct *)conv;
  conv_im2col->oc_tile_ = C16NUM;
  conv_im2col->row_tile_ = C6NUM;
  conv_im2col->row_major_to_col_nmajor_ = RowMajor2Col16Major;
}

int ConvIm2ColAVXInitTmpBuffer(ConvolutionIm2ColBaseStruct *conv_im2col) {
  int kernel_chw = conv_im2col->conv_.compute_.kernel_hw_ * conv_im2col->conv_.compute_.in_c_;
  NNACL_CHECK_INT_MUL_NOT_OVERFLOW(kernel_chw, conv_im2col->conv_.base_.thread_nr_, NNACL_ERR);
  int total_kernel_chw = kernel_chw * conv_im2col->conv_.base_.thread_nr_;
  NNACL_CHECK_INT_MUL_NOT_OVERFLOW(total_kernel_chw, conv_im2col->row_tile_, NNACL_ERR);
  int unit_size = total_kernel_chw * conv_im2col->row_tile_;

  ExecEnv *env = conv_im2col->conv_.base_.env_;
  NNACL_CHECK_NULL_RETURN_ERR(env);

  if (conv_im2col->packed_input_ != NULL) {
    env->Free(env->allocator_, conv_im2col->packed_input_);
    conv_im2col->packed_input_ = NULL;
  }
  conv_im2col->packed_input_ = env->Alloc(env->allocator_, unit_size * sizeof(float));
  NNACL_MALLOC_CHECK_NULL_RETURN_ERR(conv_im2col->packed_input_);

  if (conv_im2col->col_major_input_ != NULL) {
    env->Free(env->allocator_, conv_im2col->col_major_input_);
    conv_im2col->col_major_input_ = NULL;
  }
  conv_im2col->col_major_input_ = env->Alloc(env->allocator_, unit_size * sizeof(float));
  NNACL_MALLOC_CHECK_NULL_RETURN_ERR(conv_im2col->col_major_input_);

  conv_im2col->output_need_align_ =
    conv_im2col->conv_.compute_.out_c_ % conv_im2col->oc_tile_ != 0 && conv_im2col->conv_.out_format_ == Format_NC4HW4;
  if (conv_im2col->output_need_align_) {
    int oc_algin = UP_DIV(conv_im2col->conv_.compute_.out_c_, conv_im2col->oc_tile_);
    int output_bhw = conv_im2col->conv_.compute_.out_n_ * conv_im2col->conv_.compute_.out_hw_;
    NNACL_CHECK_INT_MUL_NOT_OVERFLOW(output_bhw, conv_im2col->oc_tile_ * oc_algin, NNACL_ERR);
    int pack_output_size = output_bhw * conv_im2col->oc_tile_ * oc_algin;

    if (conv_im2col->tmp_output_ != NULL) {
      env->Free(env->allocator_, conv_im2col->tmp_output_);
      conv_im2col->tmp_output_ = NULL;
    }
    conv_im2col->tmp_output_ = env->Alloc(env->allocator_, pack_output_size * sizeof(float));
    NNACL_MALLOC_CHECK_NULL_RETURN_ERR(conv_im2col->tmp_output_);
  }
  return NNACL_OK;
}

int ConvIm2ColAVXRunImpl(struct ConvolutionBaseStruct *conv, int task_id) {
  ConvolutionIm2ColBaseStruct *conv_im2col = (ConvolutionIm2ColBaseStruct *)conv;
  NNACL_CHECK_NULL_RETURN_ERR(conv_im2col);
  ConvParameter *conv_param = (ConvParameter *)conv->base_.param_;
  NNACL_CHECK_NULL_RETURN_ERR(conv_param);
  float *ori_input_data = conv->base_.in_[FIRST_INPUT]->data_;
  NNACL_CHECK_NULL_RETURN_ERR(ori_input_data);

  if (conv->out_format_ != Format_NC4HW4) {
    if (conv->use_batch_cut_flag_) {
      ConvFp32CutByBatch(ori_input_data, conv_im2col->packed_input_, (float *)conv->packed_weight_,
                         (float *)conv->bias_data_, conv_im2col->col_major_input_, conv_im2col->tmp_output_, task_id,
                         conv_param);
    } else {
      ConvFp32(ori_input_data, conv_im2col->packed_input_, (float *)conv->packed_weight_, (float *)conv->bias_data_,
               conv_im2col->col_major_input_, conv_im2col->tmp_output_, task_id, conv_param);
    }
  } else {
    ConvFp32OutNC4HW4(ori_input_data, conv_im2col->packed_input_, (float *)conv->packed_weight_,
                      (float *)conv->bias_data_, conv_im2col->col_major_input_, conv_im2col->tmp_output_, task_id,
                      conv_param);
  }
  return NNACL_OK;
}

int ConvolutionIm2colAvxCompute(KernelBase *self) {
  ConvolutionIm2ColBaseStruct *conv_im2col = (ConvolutionIm2ColBaseStruct *)self;
  NNACL_CHECK_NULL_RETURN_ERR(conv_im2col);

  int ret = conv_im2col->init_tmp_buffer_(conv_im2col);
  if (ret != NNACL_OK) {
    ConvIm2ColBaseFreeTmpBuffer(conv_im2col);
    return ret;
  }

  float *output_addr = (float *)self->out_[OUTPUT_INDEX]->data_;
  NNACL_CHECK_NULL_RETURN_ERR(output_addr);
  if (!conv_im2col->output_need_align_) {
    conv_im2col->tmp_output_ = output_addr;
  }

  ret = ConvBaseRepackWeight(&conv_im2col->conv_);
  if (ret != NNACL_OK) {
    ConvIm2ColBaseFreeTmpBuffer(conv_im2col);
    return ret;
  }

  ret = self->env_->ParallelLaunch(self->env_->thread_pool_, ConvIm2ColBaseImpl, self, self->thread_nr_);

  if (conv_im2col->output_need_align_) {
    PackNC8HW8AlignedToNC8HW8NotAlignedFp32(conv_im2col->tmp_output_, output_addr, conv_im2col->conv_.compute_.out_n_,
                                            conv_im2col->conv_.compute_.out_w_ * conv_im2col->conv_.compute_.out_h_,
                                            conv_im2col->conv_.compute_.out_c_);
  } else {
    conv_im2col->tmp_output_ = NULL;
  }

  ConvIm2ColBaseFreeTmpBuffer(conv_im2col);
  return ret;
}

ConvolutionBaseStruct *CreateConvIm2ColAVX(ConvParameter *conv_param) {
  ConvolutionIm2ColBaseStruct *conv_im2col = (ConvolutionIm2ColBaseStruct *)malloc(sizeof(ConvolutionIm2ColBaseStruct));
  NNACL_MALLOC_CHECK_NULL_RETURN_NULL(conv_im2col);
  memset(conv_im2col, 0, sizeof(ConvolutionIm2ColBaseStruct));

  conv_im2col->init_tmp_buffer_ = ConvIm2ColAVXInitTmpBuffer;

  conv_im2col->conv_.malloc_weight_bias_ = ConvIm2ColBaseMallocWeightBiasData;
  conv_im2col->conv_.init_global_variable_ = ConvIm2ColAVXInitGlobalVariable;
  conv_im2col->conv_.run_impl_ = ConvIm2ColAVXRunImpl;
  conv_im2col->conv_.pack_weight_ = ConvIm2ColBasePackWeight;

  conv_im2col->conv_.base_.Compute = ConvolutionIm2colAvxCompute;
  conv_im2col->conv_.base_.Prepare = ConvolutionIm2colBasePrepare;
  conv_im2col->conv_.base_.Resize = ConvolutionIm2colBaseResize;
  conv_im2col->conv_.base_.Release = ConvolutionIm2colBaseRelease;

  return (ConvolutionBaseStruct *)conv_im2col;
}
#endif
