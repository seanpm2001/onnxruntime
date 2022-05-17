// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#pragma once
#include "core/common/common.h"
#include "core/framework/op_kernel.h"
#include "beam_search_shared.h"

namespace onnxruntime {
namespace contrib {
namespace transformers {

// bugbug: IBeamSearchParameters only contains shared parameters?
struct GreedySearchParameters : public IBeamSearchParameters {
  Status Validate() const;

  void ParseFromAttributes(const OpKernelInfo& info);

  void ParseFromInputs(OpKernelContext* context);

  void SetSubgraphParameters(int vocab_size, int num_heads, int head_size, int num_layers);
};

}  // namespace transformers
}  // namespace contrib
}  // namespace onnxruntime