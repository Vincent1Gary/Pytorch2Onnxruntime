/*
 * SPDX-License-Identifier: Apache-2.0
 */
#include "core/session/onnxruntime_c_api.h"
#include <iostream>
#include "custom_op_infer.h"
#include <cassert>
#include <vector>
#include "/root/workspace/onnxruntime/onnxruntime/test/util/include/test_allocator.h"

// #include <THC/THC.h>
#define USE_CUDA 1
typedef const char* PATH_TYPE;
#define TSTR(X) (X)
static constexpr PATH_TYPE CUSTOM_OP_MODEL_URI = TSTR("/root/workspace/onnxruntime_inference_test/fps_custom_infer/fps_model1.onnx");

#define ORT_ABORT_ON_ERROR(expr)                             \
  do {                                                       \
    OrtStatus* onnx_status = (expr);                         \
    if (onnx_status != NULL) {                               \
      const char* msg = g_ort->GetErrorMessage(onnx_status); \
      fprintf(stderr, "%s\n", msg);                          \
      g_ort->ReleaseStatus(onnx_status);                     \
      abort();                                               \
    }                                                        \
  } while (0);

const OrtApi* g_ort =OrtGetApiBase()->GetApi(ORT_API_VERSION);
// if (!g_ort) {
//     fprintf(stderr, "Failed to init ONNX Runtime engine.\n");
//     return -1;
// }

std::string print_shape(const std::vector<int64_t>& v) {
  std::stringstream ss("");
  for (size_t i = 0; i < v.size() - 1; i++)
    ss << v[i] << "x";
  ss << v[v.size() - 1];
  return ss.str();
}

// template<class T>
// T dimProduct(int64_t v)
// {
//         return std::accumulate(v.begin(), v.end(), 1, std::multiplies<T>());
// }


int enable_cuda(OrtSessionOptions* session_options) {
  // OrtCUDAProviderOptions is a C struct. C programming language doesn't have constructors/destructors.
  OrtCUDAProviderOptions o;
  // Here we use memset to initialize every field of the above data struct to zero.
  memset(&o, 0, sizeof(o));
  // But is zero a valid value for every variable? Not quite. It is not guaranteed. In the other words: does every enum
  // type contain zero? The following line can be omitted because EXHAUSTIVE is mapped to zero in onnxruntime_c_api.h.
  o.cudnn_conv_algo_search = OrtCudnnConvAlgoSearch::EXHAUSTIVE;
  o.gpu_mem_limit = SIZE_MAX;
  OrtStatus* onnx_status = g_ort->SessionOptionsAppendExecutionProvider_CUDA(session_options, &o);
  if (onnx_status != NULL) {
    const char* msg = g_ort->GetErrorMessage(onnx_status);
    fprintf(stderr, "%s\n", msg);
    g_ort->ReleaseStatus(onnx_status);
    return -1;
  }
  return 0;
}

// template <typename T1, typename T2, typename T3>
// void cuda_add(int64_t, T3*, const T1*, const T2*, cudaStream_t compute_stream);

// static constexpr PATH_TYPE CUSTOM_OP_MODEL_URI = TSTR("testdata/foo_1.onnx");
// extern unique_ptr<Ort::Env> ort_env;
OrtCUDAProviderOptions CreateDefaultOrtCudaProviderOptionsWithCustomStream(void* cuda_compute_stream) {
  OrtCUDAProviderOptions cuda_options;
  cuda_options.do_copy_in_default_stream = true;
  cuda_options.has_user_compute_stream = cuda_compute_stream != nullptr ? 1 : 0;
  cuda_options.user_compute_stream = cuda_compute_stream;
  return cuda_options;
}

template <typename OutT>
void RunSession(Ort::MemoryInfo memoryinfo, OrtAllocator* allocator, Ort::Session& session_object,
                const std::vector<Input>& inputs,
                const char* output_name,
                const std::vector<int64_t>& dims_y,
                const std::vector<OutT>& values_y,
                Ort::Value* output_tensor) {
  // 构建模型输入
  std::vector<Ort::Value> ort_inputs;
  std::vector<const char*> input_names;
  // auto memory_info = Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeCPU);

  for (size_t i = 0; i < inputs.size(); i++) {
    input_names.emplace_back(inputs[i].name);
    ort_inputs.emplace_back(
        Ort::Value::CreateTensor<float>(memoryinfo, const_cast<float*>(inputs[i].values.data()),
                                        inputs[i].values.size(), inputs[i].dims.data(), inputs[i].dims.size()));
        // Ort::Value::CreateTensor<float>(memory_info, const_cast<float*>(inputs[i].values.data()),
        //                         inputs[i].values.size(), inputs[i].dims.data(), inputs[i].dims.size()));
  }
  // 运行 RUN
  std::vector<Ort::Value> ort_outputs;
  if (output_tensor)
    session_object.Run(Ort::RunOptions{nullptr}, input_names.data(), ort_inputs.data(), ort_inputs.size(),
                       &output_name, output_tensor, 1);
  else {
    ort_outputs = session_object.Run(Ort::RunOptions{}, input_names.data(), ort_inputs.data(), ort_inputs.size(),
                                     &output_name, 1);
    ASSERT_EQ(ort_outputs.size(), 1u);
    output_tensor = &ort_outputs[0];
    std::cout<<"output_tensor: "<<output_tensor<<std::endl;
  }

  auto type_info = output_tensor->GetTensorTypeAndShapeInfo();
  ASSERT_EQ(type_info.GetShape(), dims_y);
  size_t total_len = type_info.GetElementCount();
  std::cout<<"total_len:" <<total_len<<std::endl;
  std::vector<int64_t> output_node_dims;  // simplify... this model has only 1 input node {1, 3, 224, 224}.
      // print input shapes/dims
    output_node_dims = type_info.GetShape();
    printf("num_dims=%zu\n", output_node_dims.size());
    for (int j = 0; j < output_node_dims.size(); j++) printf(" dim %d=%jd\n",  j, output_node_dims[j]);
  
  ASSERT_EQ(values_y.size(), total_len);
  OutT* f = output_tensor->GetTensorMutableData<OutT>();
  // for (size_t i = 0; i != 4096; ++i) {
  //   std::cout<<"output: "<<f[i]<<std::endl;
  // }
  for (size_t i = 0; i != total_len; ++i) {
    ASSERT_EQ(values_y[i], f[i]);
  }

}


template <typename T>
static void TestInference(Ort::Env& env, const std::basic_string<ORTCHAR_T>& model_uri,
                   const std::vector<Input>& inputs,
                   const char* output_name,
                   const std::vector<int64_t>& expected_dims_y,
                   const std::vector<float>& expected_values_y,
                   OrtCustomOpDomain* custom_op_domain_ptr,
                    void* cuda_compute_stream = nullptr) {
  Ort::SessionOptions session_options;
  std::cout << "Running simple inference with default provider" << std::endl;
    // auto cuda_options = CreateDefaultOrtCudaProviderOptionsWithCustomStream(cuda_compute_stream);
  // OrtCUDAProviderOptions cuda_options;
  // cuda_options.device_id = 0;
  // cuda_options.arena_extend_strategy = 0;
  // cuda_options.gpu_mem_limit = std::numeric_limits<size_t>::max();
  // cuda_options.cudnn_conv_algo_search = OrtCudnnConvAlgoSearch::EXHAUSTIVE;
  // cuda_options.do_copy_in_default_stream = 1;
  // session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

  printf("Try to enable CUDA first\n");
   int  ret = enable_cuda(session_options);
    if (ret) {
      fprintf(stderr, "CUDA is not available\n");
    } else {
      printf("CUDA is enabled\n");
  }
  if (custom_op_domain_ptr) {
    session_options.Add(custom_op_domain_ptr);
  }

  // Ort::Session session(env, model_uri.c_str(), session_options);
  OrtSession* session;
  ORT_ABORT_ON_ERROR(g_ort->CreateSession(env, model_uri.c_str(), session_options, &session));
  Ort::AllocatorWithDefaultOptions allocator;
  size_t input_count, output_count;
  ORT_ABORT_ON_ERROR(g_ort->SessionGetInputCount(session, &input_count));
  std::cout<<"input_count: "<<input_count<<std::endl;
  ORT_ABORT_ON_ERROR(g_ort->SessionGetOutputCount(session, &output_count));
  std::cout<<"out_count: "<<output_count<<std::endl;

  std::vector<std::string> inputNames(input_count);
  for (size_t i = 0; i < input_count; i++)
  {
     char* name;
     ORT_ABORT_ON_ERROR(g_ort->SessionGetInputName(session, i, allocator, &name));
     inputNames.push_back(name);
  }

  OrtTypeInfo* input_type_info;
   ORT_ABORT_ON_ERROR(g_ort->SessionGetInputTypeInfo(session, 0,  &input_type_info));
  ONNXType input_type;
  g_ort->GetOnnxTypeFromTypeInfo(input_type_info, &input_type);

  const OrtTensorTypeAndShapeInfo* tensor_info;
  g_ort->CastTypeInfoToTensorInfo(input_type_info, &tensor_info);

  ONNXTensorElementDataType tensor_type;
  g_ort->GetTensorElementType(tensor_info, &tensor_type);

  size_t dim_count;
  g_ort->GetDimensionsCount(tensor_info, &dim_count);
  std::cout<<"dim_count: "<<dim_count<<std::endl;
  int64_t dim_values[dim_count]; 
  g_ort->GetDimensions(tensor_info, dim_values, dim_count);
  std::cout<<"dim_values: "<<dim_values[0]<<" "<<dim_values[1]<<" "<<dim_values[2]<<std::endl;
  size_t inputTensorSize=1;
  for (size_t i = 0; i < dim_count; i++)   inputTensorSize*=dim_values[i];
  std::cout<<"inputTensorSize: "<<inputTensorSize<<std::endl;
  
  g_ort->ReleaseTypeInfo(input_type_info);

      // auto inputTensorInfo = inputTypeInfo->GetTensorTypeAndShapeInfo();
      // std:: vector<int64_t> inputDims = inputTensorInfo.GetShape();
      // size_t inputTensorSize = vectorProduct(inputDims);
      // std::cout << "inputTensorSize: " << inputTensorSize<< std::endl;
      // std::cout << "inputDims: " << inputDims[0]<< " "<<inputDims[1]<<" "<<inputDims[2]<< std::endl;
  
  // std::string inputName = session->GetInputName(0, allocator);
  //  std::vector<std::string> inputNames{inputName};

  // Ort::TypeInfo inputTypeInfo = session->GetInputTypeInfo(0);
  // auto inputTensorInfo = inputTypeInfo.GetTensorTypeAndShapeInfo();
  // std:: vector<int64_t> inputDims = inputTensorInfo.GetShape();
  // size_t inputTensorSize = vectorProduct(inputDims);
  // std::cout << "inputTensorSize: " << inputTensorSize<< std::endl;
  // std:: vector<Ort::Value> inputTensors;
  // std::cout << "inputDims: " << inputDims[0]<< " "<<inputDims[1]<<" "<<inputDims[2]<< std::endl;

  // auto memory_info = Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeCPU);
  // auto default_allocator = std::make_unique<MockedOrtAllocator>();
  // Ort::MemoryInfo memory_info("Cuda", OrtDeviceAllocator, 0, OrtMemTypeDefault);  // GPU memory


  // std::vector<const char*> output_node_names = {"output"};
  // std::vector<Ort::Value> ort_outputs;
   OrtMemoryInfo* memory_info;
  ORT_ABORT_ON_ERROR(g_ort->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &memory_info));
  const int64_t input_shape[] = {dim_values[0], dim_values[1],dim_values[2]};
  const size_t input_shape_len = sizeof(input_shape) / sizeof(input_shape[0]);
  const size_t model_input_len = inputTensorSize * sizeof(float);

  OrtValue* input_tensor = NULL;
  ORT_ABORT_ON_ERROR(g_ort->CreateTensorWithDataAsOrtValue(memory_info, const_cast<float*>(inputs[0].values.data()), model_input_len, dim_values,
                                                           input_shape_len, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
                                                           &input_tensor));
  assert(input_tensor != NULL);
  int is_tensor;
  ORT_ABORT_ON_ERROR(g_ort->IsTensor(input_tensor, &is_tensor));
  assert(is_tensor);
  g_ort->ReleaseMemoryInfo(memory_info);
  std::cout << "session.run" << std::endl;
  // session.Run(Ort::RunOptions{nullptr}, input_names.data(), input_tensors.data(), input_tensors.size(), output_node_names.data(), 1);
  OrtValue* output_tensor = NULL;
   const char* input_names[] = {"input"};
  const char* output_names[] = {"output"};
  ORT_ABORT_ON_ERROR(g_ort->Run(session, NULL, input_names, (const OrtValue* const*)&input_tensor, 1, output_names, 1,
                                &output_tensor));
  std::cout << "session.end" << std::endl;

 assert(output_tensor != NULL);
  ORT_ABORT_ON_ERROR(g_ort->IsTensor(output_tensor, &is_tensor));
  assert(is_tensor);
  
  // Ort::Value output_tensor{nullptr};
  // output_tensor = Ort::Value::CreateTensor<float>(memory_info, const_cast<float*>(expected_values_y.data()), expected_values_y.size(), expected_dims_y.data(), expected_dims_y.size());
  // assert(ort_outputs.size() == 1);

  // auto type_info = *output_tensor.GetTensorTypeAndShapeInfo();
  // assert(type_info.GetShape() == expected_dims_y);
  // size_t total_len = type_info.GetElementCount();
  // assert(expected_values_y.size() == total_len);


  // float* f = *output_tensor.GetTensorMutableData<float>();
  // for (size_t i = 0; i != total_len; ++i) {
  //   assert(expected_values_y[i] == f[i]);
  // }
  g_ort->ReleaseSessionOptions(session_options);
  g_ort->ReleaseSession(session);
  g_ort->ReleaseEnv(env);

}

// template <typename OutT>
// static void TestInference(Ort::Env& env, const std::basic_string<ORTCHAR_T>& model_uri,
//                           const std::vector<Input>& inputs,
//                           const char* output_name,
//                           const std::vector<int64_t>& expected_dims_y,
//                           const std::vector<OutT>& expected_values_y,
//                           int provider_type,
//                           OrtCustomOpDomain* custom_op_domain_ptr,
//                           const char* custom_op_library_filename,
//                           void** library_handle = nullptr,
//                           bool test_session_creation_only = false,
//                           void* cuda_compute_stream = nullptr) {
//   Ort::SessionOptions session_options;
//   if (provider_type == 1) {
// #ifdef USE_CUDA
//     std::cout << "Running simple inference with cuda provider" << std::endl;
//     std::cout<<"test cuda_compute_stream: "<<cuda_compute_stream<<std::endl;
//     auto cuda_options = CreateDefaultOrtCudaProviderOptionsWithCustomStream(cuda_compute_stream);
//     session_options.AppendExecutionProvider_CUDA(cuda_options);
// #else
//     ORT_UNUSED_PARAMETER(cuda_compute_stream);
//     return;
// #endif
//   } else if (provider_type == 2) {
// #ifdef USE_DNNL
//     Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_Dnnl(session_options, 1));
//     std::cout << "Running simple inference with dnnl provider" << std::endl;
// #else
//     return;
// #endif
//   } else if (provider_type == 3) {
// #ifdef USE_NUPHAR
//     Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_Nuphar(session_options,
//                                                                       /*allow_unaligned_buffers*/ 1, ""));
//     std::cout << "Running simple inference with nuphar provider" << std::endl;
// #else
//     return;
// #endif
//   } else {
//     std::cout << "Running simple inference with default provider" << std::endl;
//   }
//    std::cout << "Running session_options.Add" << std::endl;

//   if (custom_op_domain_ptr) {
//     session_options.Add(custom_op_domain_ptr);
//   }

//   if (custom_op_library_filename) {
//     Ort::ThrowOnError(Ort::GetApi().RegisterCustomOpsLibrary(session_options,
//                                                              custom_op_library_filename, library_handle));
//     std::cout << "custom_op_library_filename" << custom_op_library_filename<<std::endl;
//   }
//   // if session creation passes, model loads fine
//   std::cout<<"model_uri.c_str()： "<<model_uri.c_str()<<std::endl;
//   Ort::Session session(env, model_uri.c_str(), session_options);
//   // caller wants to test running the model (not just loading the model)
//   if (!test_session_creation_only) {
//     // Now run
//     auto memory_info = Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeCPU);
//     auto default_allocator = make_unique<MockedOrtAllocator>();

//     //without preallocated output tensor
//     // RunSession<OutT>(default_allocator.get(),
//     //                  session,
//     //                  inputs,
//     //                  output_name,
//     //                  expected_dims_y,
//     //                  expected_values_y,
//     //                  nullptr);
//     //with preallocated output tensor
//     // Ort::Value value_y = Ort::Value::CreateTensor<float>(default_allocator.get(),
//     //                                                      expected_dims_y.data(), expected_dims_y.size());
//     // Ort::Value value_y{nullptr};
//     //test


//     Ort::Value value_y = Ort::Value::CreateTensor<float>(memory_info,
//                                                          expected_dims_y.data(), expected_dims_y.size());
//     std::cout<<"expected_dims_y.data(): "<<expected_dims_y.data()<<" expected_dims_y.size(): "<<expected_dims_y.size()<<std::endl;
//     //test it twice
//     std::cout<<"session run start"<<std::endl;
//     RunSession<OutT>(memory_info, default_allocator.get(),
//                        session,
//                        inputs,
//                        output_name,
//                        expected_dims_y,
//                        expected_values_y,
//                        &value_y);
  
//     std::cout<<"session run end"<<std::endl;
//     std::cout<<"expected_dims_y.data(): "<<expected_dims_y.data()<<" expected_dims_y.size(): "<<expected_dims_y.size()<<std::endl;
//   }
// }
// file path: onnxruntime/test/shared_lib/test_inference.cc

 int main(int argc, char** argv) {
  std::cout << "Running custom op inference" << std::endl;
  
  auto ort_env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "Default");

  std::vector<Input> inputs(1);
  Input& input = inputs[0];
  input.name = "input";
  input.dims = {1,16384,4};
  std::vector<float> v(16384 * 4);
   std::generate(v.begin(), v.end(), [&] { return rand() / 255; });
   input.values = v;
  std::cout<<"input.value[0]: "<<input.values[0]<<std::endl;
  // prepare expected inputs and outputs
  std::vector<int64_t> expected_dims_y = {1,4096};
  std::vector<float> expected_values_y(4096);
  std::generate(expected_values_y.begin(), expected_values_y.end(), [&] { return rand() / 255; }); 

  // 创建定制算子（MyCustomOp）
  cudaStream_t compute_stream = nullptr;    // 声明一个 cuda stream
  cudaStreamCreateWithFlags(&compute_stream, cudaStreamNonBlocking);  // 创建一个 cuda stream
  std::cout<<"compute_stream1: "<<compute_stream<<std::endl;
  FpsCustomOp custom_op{onnxruntime::kCudaExecutionProvider, compute_stream};
  // FpsCustomOp custom_op_cpu{onnxruntime::kCpuExecutionProvider, nullptr};
  
  // 创建定制算子域（CustomOpDomain）
  Ort::CustomOpDomain custom_op_domain("mydomain");
  // 在定制算子域中添加定制算子
  custom_op_domain.Add(&custom_op);
  // custom_op_domain.Add(&custom_op);

  // 进入 TestInference
#ifdef USE_CUDA
  // TestInference<float>(*ort_env, CUSTOM_OP_MODEL_URI, inputs, "output", expected_dims_y, expected_values_y, 1,
  //                      custom_op_domain, nullptr, nullptr, false, compute_stream);
  TestInference<float>(*ort_env, CUSTOM_OP_MODEL_URI, inputs, "output", expected_dims_y, expected_values_y,  custom_op_domain,  compute_stream);
  cudaStreamDestroy(compute_stream);
#else
  TestInference<float>(*ort_env, CUSTOM_OP_MODEL_URI, inputs, "Y", expected_dims_y, expected_values_y, 0,
                       custom_op_domain, nullptr);
#endif

}
