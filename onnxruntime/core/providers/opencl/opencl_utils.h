// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#pragma once

#include <CL/opencl.h>
#ifdef CL3W_ENABLE
#include <cl3w.h>
#endif

#include <Tracy.hpp>
#include <TracyOpenCL.hpp>

#include <cstdio>
#include <iomanip>
#include <sstream>
#include <utility>
#include "core/framework/op_kernel.h"
#include "core/framework/tensor.h"
#include "opencl_forward_decl.h"
#include "opencl_execution_provider.h"

#ifdef NDEBUG
#define USE_CL_CHECKED_CAST 0
#else
#define USE_CL_CHECKED_CAST 1
#endif

#define ONNX_OPENCL_OPERATOR_KERNEL(name, ver, builder, ...) \
  ONNX_OPERATOR_KERNEL_EX(name, kOnnxDomain, ver, kOpenCLExecutionProvider, builder, __VA_ARGS__)

#define OPENCL_EXEC_PROVIDER_FROM_INFO(info) \
  const_cast<OpenCLExecutionProvider*>(static_cast<const OpenCLExecutionProvider*>((info).GetExecutionProvider()))

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define ORT_RETURN_IF_CL_ERROR(error_code, ...)                                          \
  if ((error_code) != CL_SUCCESS) {                                                      \
    std::ostringstream oss;                                                              \
    oss << __FILE__ ":" << __LINE__                                                      \
        << "\nOpenCL Error Code  : " << (int)(error_code)                                \
        << "\n       Error String: " << onnxruntime::opencl::GetErrorString(error_code); \
    return ORT_MAKE_STATUS(ONNXRUNTIME, EP_FAIL, oss.str(),                              \
                           ::onnxruntime::MakeString(__VA_ARGS__));                      \
  }

#define ORT_THROW_IF_CL_ERROR(error_code, ...)                                           \
  if ((error_code) != CL_SUCCESS) {                                                      \
    std::ostringstream oss;                                                              \
    oss << __FILE__ ":" << __LINE__                                                      \
        << "\nOpenCL Error Code  : " << (int)(error_code)                                \
        << "\n       Error String: " << onnxruntime::opencl::GetErrorString(error_code); \
    ORT_THROW(oss.str(), ::onnxruntime::MakeString(__VA_ARGS__));                        \
  }

#if USE_CL_CHECKED_CAST
#define CL_CHECK_MEM_OBJECT_IS_BUFFER(mem)                                                                          \
  {                                                                                                                 \
    cl_mem_object_type ret;                                                                                         \
    ORT_THROW_IF_CL_ERROR(clGetMemObjectInfo((cl_mem)mem, CL_MEM_TYPE, sizeof(cl_mem_object_type), &ret, nullptr)); \
    ORT_ENFORCE(ret == CL_MEM_OBJECT_BUFFER, mem, " is not Buffer");                                                \
  }
#define CL_CHECK_MEM_OBJECT_IS_IMAGE_2D(mem)                                                                        \
  {                                                                                                                 \
    cl_mem_object_type ret;                                                                                         \
    ORT_THROW_IF_CL_ERROR(clGetMemObjectInfo((cl_mem)mem, CL_MEM_TYPE, sizeof(cl_mem_object_type), &ret, nullptr)); \
    ORT_ENFORCE(ret == CL_MEM_OBJECT_IMAGE2D, mem, " is not Image2D");                                              \
  }

#define CL_BUFFER_FROM_TENSOR(TENSOR) [&]() {                                                             \
  cl_mem ptr = const_cast<cl_mem>(static_cast<const std::remove_pointer_t<cl_mem>*>((TENSOR).DataRaw())); \
  if (ptr) {                                                                                              \
    CL_CHECK_MEM_OBJECT_IS_BUFFER(ptr);                                                                   \
  }                                                                                                       \
  return ptr;                                                                                             \
}()

#define CL_IMAGE2D_FROM_TENSOR(TENSOR) [&]() {                                                            \
  cl_mem ptr = const_cast<cl_mem>(static_cast<const std::remove_pointer_t<cl_mem>*>((TENSOR).DataRaw())); \
  if (ptr) {                                                                                              \
    CL_CHECK_MEM_OBJECT_IS_IMAGE_2D(ptr);                                                                 \
  }                                                                                                       \
  return ptr;                                                                                             \
}()
#else
#define CL_CHECK_MEM_OBJECT_IS_BUFFER(mem)
#define CL_CHECK_MEM_OBJECT_IS_IMAGE_2D(mem)
#define CL_BUFFER_FROM_TENSOR(TENSOR) const_cast<cl_mem>(static_cast<const std::remove_pointer_t<cl_mem>*>((TENSOR).DataRaw()))
#define CL_IMAGE2D_FROM_TENSOR(TENSOR) const_cast<cl_mem>(static_cast<const std::remove_pointer_t<cl_mem>*>((TENSOR).DataRaw()))
#endif

#define VLOG_CL_NODE()                                          \
  VLOGS_DEFAULT(0) << "[CL] Node: " << context->GetNodeName()   \
                   << ", num inputs: " << context->InputCount() \
                   << ", num outputs: " << context->OutputCount()
#define VLOG_CL_BUFFER(desc, tensor_ptr)                                      \
  VLOGS_DEFAULT(0) << "[CL]  " << std::setfill(' ') << std::setw(9) << (desc) \
                   << " shape " << (tensor_ptr)->Shape()                      \
                   << "Buffer(" << CL_BUFFER_FROM_TENSOR(*(tensor_ptr)) << ")"
#define VLOG_CL_IMAGE2D(desc, tensor_ptr)                                     \
  VLOGS_DEFAULT(0) << "[CL]  " << std::setfill(' ') << std::setw(9) << (desc) \
                   << " shape " << (tensor_ptr)->Shape()                      \
                   << "Image2D(" << CL_IMAGE2D_FROM_TENSOR(*(tensor_ptr)) << ")"

namespace onnxruntime {
namespace opencl {

class NDRange {
 public:
  NDRange() : size_(0), values_{0, 0, 0} {}

  template <typename T>
  explicit NDRange(T x) : size_(1), values_{static_cast<size_t>(x), 0, 0} {}

  template <typename T1, typename T2>
  NDRange(T1 x, T2 y) : size_(2), values_{static_cast<size_t>(x), static_cast<size_t>(y), 0} {}

  template <typename T1, typename T2, typename T3>
  NDRange(T1 x, T2 y, T3 z) : size_(3), values_{static_cast<size_t>(x), static_cast<size_t>(y), static_cast<size_t>(z)} {}

  uint8_t Size() const { return size_; }

  const size_t* Data() const {
    if (size_ != 0) {
      return values_;
    }
    return nullptr;
  }

  inline std::string ToString() const {
    if (size_ == 0) {
      return "[<unspecified>]";
    }
    if (size_ == 1) {
      return onnxruntime::MakeString("[", values_[0], "]");
    }
    if (size_ == 2) {
      return onnxruntime::MakeString("[", values_[0], ",", values_[1], "]");
    }
    return onnxruntime::MakeString("[", values_[0], ",", values_[1], ",", values_[2], "]");
  }

private:
  uint8_t size_;
  size_t values_[3];
};

const char* GetErrorString(cl_int error_code);

// NOTE: for OrtDevice ctor
struct CLMemType {
  static constexpr OrtDevice::MemoryType OPENCL_IMAGE_2D = OrtDevice::MemType::DEFAULT;
  static constexpr OrtDevice::MemoryType OPENCL_BUFFER = 5;
};

// NOTE: for opencl internal definition.
enum MemoryKind : uint8_t {
  Buffer = CLMemType::OPENCL_BUFFER,
  Image2D = CLMemType::OPENCL_IMAGE_2D,
};

template <typename T, typename E = std::enable_if_t<std::is_integral_v<T>>>
T CeilDiv(T a, T b) {
  return (a - 1) / b + 1;
}

template <typename T1, typename T2, typename E = std::enable_if_t<std::is_integral_v<T1> && std::is_integral_v<T2>>>
T1 CeilDiv(T1 a, T2 b) {
  return (a - 1) / b + 1;
}

template <typename T1, typename T2, typename E = std::enable_if_t<std::is_integral_v<T1> && std::is_integral_v<T2>>>
T1 RoundToMultiple(T1 a, T2 m) {
  return CeilDiv(a, m) * m;
}

class Image2DDesc : private std::pair<int64_t, int64_t> {
 public:
  friend struct ::std::hash<Image2DDesc>;
  using pair::pair;

  static Image2DDesc PackFromTensor(const TensorShape& shape) {
    switch (shape.NumDimensions()) {
      case 1:
        return PackFromTensor1D(shape);
      case 2:
        return PackFromTensor2D(shape);
      case 4:
        return PackFromTensorNCHW(shape);
      case 5:
        return PackFromTensorNCHWc(shape);
      default:
        return {0, 0};
    }
  }

  static Image2DDesc PackFromTensor1D(const TensorShape& shape) {
    ORT_ENFORCE(shape.NumDimensions() == 1);
    //
    return {1024, CeilDiv(shape[0], 4 * 1024)};
  }

  static Image2DDesc PackFromTensor2D(const TensorShape& shape) {
    ORT_ENFORCE(shape.NumDimensions() == 2);
    return {CeilDiv(shape[0], 4), shape[1]};
  }

  static Image2DDesc PackFromTensorNCHW(const TensorShape& shape) {
    ORT_ENFORCE(shape.NumDimensions() == 4);
    int64_t N = shape[0];
    int64_t C = shape[1];
    int64_t H = shape[2];
    int64_t W = shape[3];
    int64_t c = 4;
    int64_t Cc = CeilDiv(C, c);
    return {Cc * W, N * H};
  }

  // NCHWc is actually Tensor of shape N[C/c]HWc then packed as NH C/cWc
  static Image2DDesc PackFromTensorNCHWc(const TensorShape& shape) {
    ORT_ENFORCE(shape.NumDimensions() == 5);
    int64_t N = shape[0];
    int64_t Cc = shape[1];
    int64_t H = shape[2];
    int64_t W = shape[3];
    int64_t c = shape[4];
    ORT_ENFORCE(c == 4);
    return {Cc * W, N * H};
  }

  static Image2DDesc PackFromConv2DWeight(const TensorShape& shape) {
    ORT_ENFORCE(shape.NumDimensions() == 4);
    int64_t C_o = shape[0];
    int64_t C_i = shape[1];
    int64_t K_h = shape[2];
    int64_t K_w = shape[3];
    return {C_i, CeilDiv(C_o, 4) * K_h * K_w};
  }
  static Image2DDesc PackFromWinogradTransform(const TensorShape& shape) {
    ORT_ENFORCE(shape.NumDimensions() == 4);
    int64_t C_o = shape[0];
    int64_t C_i = shape[1];
    //FIXME: asumme we only surpport window-size=4
    int64_t K_h = 4;
    int64_t K_w = 4;
    return {CeilDiv(C_i, 4) * K_h, 16*CeilDiv(C_o, 4)};
  }

  static Image2DDesc PackFromDepthwiseConv2DWeight(const TensorShape& shape) {
    ORT_ENFORCE(shape.NumDimensions() == 4);
    int64_t C_o = shape[0];
    int64_t C_i = shape[1];
    int64_t K_h = shape[2];
    int64_t K_w = shape[3];
    return {K_h * K_w * C_i, CeilDiv(C_o, 4)};
  }

  auto Height() const {
    return second;
  }

  auto Width() const {
    return first;
  }

  size_t UHeight() const {
    return static_cast<size_t>(second);
  }

  size_t UWidth() const {
    return static_cast<size_t>(first);
  }

  NDRange AsNDRange() const {
    return {UWidth(), UHeight()};
  }

  bool operator==(const onnxruntime::opencl::Image2DDesc& other) const {
    return Width() == other.Width() && Height() == other.Height();
  }
};

class KernelLauncher {
 public:
  explicit KernelLauncher(cl_kernel kernel) : kernel_{kernel}, index_{0}, err_{CL_SUCCESS} {}
  cl_kernel Kernel() const { return kernel_; }

#define SKIP_IF_ERRORED(expr) \
  if (err_ == CL_SUCCESS) {   \
    err_ = (expr);            \
    err_index_ = index_;      \
  }

  /**
   * @brief Set the dynamic local memory size. (aka., shared memory in CUDA).
   *
   * This function can be called multiple times.
   */
  template <typename T, typename E = std::is_convertible<T, size_t>>
  KernelLauncher& SetShmem(T num_bytes) {
    SKIP_IF_ERRORED(clSetKernelArg(kernel_, index_, num_bytes, nullptr));
    index_ += 1;
    return *this;
  }

  template <typename T, typename E = std::is_convertible<T, cl_int>>
  KernelLauncher& SetInt2(T v1, T v2) {
    cl_int tmp[2] = {static_cast<cl_int>(v1), static_cast<cl_int>(v2)};
    SKIP_IF_ERRORED(clSetKernelArg(kernel_, index_, sizeof(tmp), tmp));
    index_ += 1;
    return *this;
  }

  template <typename T, typename E = std::is_convertible<T, cl_int>>
  KernelLauncher& SetInt3(T v1, T v2, T v3) {
    cl_int3 tmp{{static_cast<cl_int>(v1), static_cast<cl_int>(v2), static_cast<cl_int>(v3)}};
    SKIP_IF_ERRORED(clSetKernelArg(kernel_, index_, sizeof(tmp), &tmp));
    index_ += 1;
    return *this;
  }

  template <typename T, typename E = std::is_convertible<T, cl_int>>
  KernelLauncher& SetInt4(T v1, T v2, T v3, T v4) {
    cl_int tmp[4] = {static_cast<cl_int>(v1), static_cast<cl_int>(v2), static_cast<cl_int>(v3), static_cast<cl_int>(v4)};
    SKIP_IF_ERRORED(clSetKernelArg(kernel_, index_, sizeof(tmp), tmp));
    index_ += 1;
    return *this;
  }

  template <typename T>
  KernelLauncher& SetArg(const T& arg) {
    SKIP_IF_ERRORED(clSetKernelArg(kernel_, index_, sizeof(T), &arg));
    index_ += 1;
    return *this;
  }

  KernelLauncher& SetBuffer(cl_mem arg) {
    SKIP_IF_ERRORED(clSetKernelArg(kernel_, index_, sizeof(cl_mem), &arg));
    index_ += 1;
    return *this;
  }

  KernelLauncher& SetBuffer(const Tensor& arg) {
    return SetBuffer(CL_BUFFER_FROM_TENSOR(arg));
  }

  template <typename T1, typename T2>
  KernelLauncher& SetBuffers(T1&& arg, T2&& other) {
    SetBuffer(std::forward<T1>(arg));
    return SetBuffer(std::forward<T2>(other));
  }

  template <typename T, typename... Ts>
  KernelLauncher& SetBuffers(T&& arg, Ts&&... args) {
    SetBuffer(std::forward<T>(arg));
    return SetBuffers(std::forward<Ts>(args)...);
  }

  KernelLauncher& SetImage2D(cl_mem arg) {
    SKIP_IF_ERRORED(clSetKernelArg(kernel_, index_, sizeof(cl_mem), &arg));
    index_ += 1;
    return *this;
  }

  KernelLauncher& SetImage2D(const Tensor& arg) {
    return SetImage2D(CL_IMAGE2D_FROM_TENSOR(arg));
  }

  template <typename T1, typename T2>
  KernelLauncher& SetImage2Ds(T1&& arg, T2&& other) {
    SetImage2D(std::forward<T1>(arg));
    return SetImage2D(std::forward<T2>(other));
  }

  template <typename T, typename... Ts>
  KernelLauncher& SetImage2Ds(T&& arg, Ts&&... args) {
    SetImage2D(std::forward<T>(arg));
    return SetImage2Ds(std::forward<Ts>(args)...);
  }

  Status Launch(const OpenCLExecutionProvider& exec, const NDRange& global, const NDRange& local = {});

 private:
  inline std::string GetKernelFunctionName() {
    size_t ret_size;
    clGetKernelInfo(kernel_, CL_KERNEL_FUNCTION_NAME, 0, nullptr, &ret_size);
    std::string ret(ret_size, '\0');
    clGetKernelInfo(kernel_, CL_KERNEL_FUNCTION_NAME, ret.size(), ret.data(), nullptr);
    return ret;
  }

#undef SKIP_IF_ERRORED
  cl_kernel kernel_;
  cl_uint index_;
  cl_int err_;
  cl_uint err_index_;
};

inline size_t HashCombine(size_t a, size_t b) {
  // https://github.com/boostorg/functional/blob/c839796c8/include/boost/functional/hash/hash.hpp#L256
  b ^= b + 0x9e3779b9 + (a << 6) + (a >> 2);
  return b;
}

// FIXME: this is debug utiltity add by Jicheng Wen
std::unique_ptr<float, std::function<void(float*)>> mapImage2dToHost(const OpenCLExecutionProvider& exec, const Tensor& tensor, int width, int height, bool write = false);
std::unique_ptr<float, std::function<void(float*)>> mapImage2dToHost(const OpenCLExecutionProvider& exec, cl_mem image, int width, int height, bool write = false);
}  // namespace opencl
}  // namespace onnxruntime

template <>
struct std::hash<onnxruntime::opencl::Image2DDesc> {
  size_t operator()(const onnxruntime::opencl::Image2DDesc& desc) const {
    std::hash<int64_t> h{};
    return onnxruntime::opencl::HashCombine(h(desc.Width()), h(desc.Height()));
  }
};