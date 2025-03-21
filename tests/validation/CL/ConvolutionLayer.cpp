/*
 * Copyright (c) 2017-2021, 2023, 2025 Arm Limited.
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include "arm_compute/core/Types.h"
#include "arm_compute/core/utils/misc/ShapeCalculator.h"
#include "arm_compute/runtime/CL/CLTensor.h"
#include "arm_compute/runtime/CL/CLTensorAllocator.h"
#include "arm_compute/runtime/CL/functions/CLConvolutionLayer.h"
#include "arm_compute/runtime/CL/functions/CLGEMMConvolutionLayer.h"
#include "tests/CL/CLAccessor.h"
#include "tests/PaddingCalculator.h"
#include "tests/datasets/LargeConvolutionLayerDataset.h"
#include "tests/datasets/SmallConvolutionLayerDataset.h"
#include "tests/datasets/TinyConvolutionLayerDataset.h"
#include "tests/framework/Asserts.h"
#include "tests/framework/Macros.h"
#include "tests/framework/datasets/Datasets.h"
#include "tests/validation/Validation.h"
#include "tests/validation/fixtures/ConvolutionLayerFixture.h"

namespace arm_compute
{
namespace test
{
namespace validation
{
using framework::dataset::make;
namespace
{
class SmallConvolutionLayerDatasetCases final : public datasets::ConvolutionLayerDataset
{
public:
    SmallConvolutionLayerDatasetCases()
    {
        // 1D Kernel
        add_config(TensorShape(1U, 130U, 2000U), TensorShape(1U, 1U, 2000U, 2000U), TensorShape(2000U), TensorShape(1U, 130U, 2000U), PadStrideInfo(1, 1, 0, 0));
    }
};

RelativeTolerance<float>            tolerance_f32(0.1f);                  /**< Tolerance value for comparing reference's output against implementation's output for DataType::F32 */
RelativeTolerance<half_float::half> tolerance_f16(half_float::half(0.2)); /**< Tolerance value for comparing reference's output against implementation's output for DataType::F16 */
constexpr AbsoluteTolerance<float>  tolerance_qasymm8(1);                 /**< Tolerance value for comparing reference's output against implementation's output for quantized data types */
constexpr float                     tolerance_num = 0.07f;                /**< Tolerance number */

/** CNN data types */
const auto CNNDataTypes = make("DataType",
{
    DataType::F16,
             DataType::F32,
             DataType::QASYMM8,
             DataType::QASYMM8_SIGNED,
});

/** Grouped CNN data types */
const auto GroupedCNNDataTypes = make("DataType",
{
    DataType::F16,
             DataType::F32
});

const auto ActivationFunctionsDataset      = make("ActivationInfo",
{
    ActivationLayerInfo(),
                        ActivationLayerInfo(ActivationLayerInfo::ActivationFunction::RELU),
                        ActivationLayerInfo(ActivationLayerInfo::ActivationFunction::BOUNDED_RELU, 0.5f),
                        ActivationLayerInfo(ActivationLayerInfo::ActivationFunction::LU_BOUNDED_RELU, 0.5f)
});
const auto ActivationFunctionsSmallDataset = make("ActivationInfo",
{
    ActivationLayerInfo(),
                        ActivationLayerInfo(ActivationLayerInfo::ActivationFunction::LU_BOUNDED_RELU, 0.5f)
});
} // namespace

TEST_SUITE(CL)
TEST_SUITE(ConvolutionLayer)

// *INDENT-OFF*
// clang-format off
DATA_TEST_CASE(ValidateConvolutionMethod, framework::DatasetMode::ALL, zip(zip(zip(zip(zip(zip(zip(
                                          make("InputInfo", { TensorInfo(TensorShape(17U, 31U, 2U), 1, DataType::F32),            // Select GEMM
                                                                                  TensorInfo(TensorShape(17U, 31U, 2U), 1, DataType::F32),            // Select GEMM
                                                                                  TensorInfo(TensorShape(23U, 27U, 5U, 4U), 1, DataType::F32),        // Select GEMM
                                                                                  TensorInfo(TensorShape(23U, 27U, 31U, 4U), 1, DataType::F32),       // Select WINOGRAD
                                                                                  TensorInfo(TensorShape(3U, 3U, 2U, 1U), 1, DataType::F32),          // Select GEMM
                                                                                  TensorInfo(TensorShape(33U, 27U, 7U, 4U), 1, DataType::F32),        // Select GEMM
                                                                                  TensorInfo(TensorShape(17U, 31U, 32U), 1, DataType::F32),           // Select WINOGRAD
                                                                                  TensorInfo(TensorShape(17U, 31U, 2U), 1, DataType::F32),            // Select GEMM
                                                                                  TensorInfo(TensorShape(17U, 31U, 2U), 1, DataType::QASYMM8_SIGNED), // Select GEMM
                                          }),
                                          make("WeightsInfo", { TensorInfo(TensorShape(5U, 5U, 2U, 19U), 1, DataType::F32),
                                                                                    TensorInfo(TensorShape(5U, 5U, 2U, 19U), 1, DataType::F32),
                                                                                    TensorInfo(TensorShape(3U, 3U, 5U, 21U), 1, DataType::F32),
                                                                                    TensorInfo(TensorShape(3U, 3U, 31U, 21U), 1, DataType::F32),
                                                                                    TensorInfo(TensorShape(3U, 3U, 5U, 21U), 1, DataType::F32),
                                                                                    TensorInfo(TensorShape(5U, 5U, 7U, 16U), 1, DataType::F16),
                                                                                    TensorInfo(TensorShape(5U, 5U, 32U, 19U), 1, DataType::F32),
                                                                                    TensorInfo(TensorShape(5U, 5U, 2U, 19U), 1, DataType::F32),
                                                                                    TensorInfo(TensorShape(5U, 5U, 2U, 19U), 1, DataType::QASYMM8_SIGNED),
                                          })),
                                          make("OutputInfo", { TensorInfo(TensorShape(15U, 15U, 19U), 1, DataType::F32),
                                                                                   TensorInfo(TensorShape(15U, 15U, 19U), 1, DataType::F32),
                                                                                   TensorInfo(TensorShape(21U, 25U, 21U, 4U), 1, DataType::F32),
                                                                                   TensorInfo(TensorShape(21U, 25U, 21U, 4U), 1, DataType::F32),
                                                                                   TensorInfo(TensorShape(11U, 25U, 21U), 1, DataType::F32),
                                                                                   TensorInfo(TensorShape(11U, 12U, 16U, 4U), 1, DataType::F32),
                                                                                   TensorInfo(TensorShape(17U, 31U, 19U), 1, DataType::F32),
                                                                                   TensorInfo(TensorShape(17U, 31U, 19U), 1, DataType::F32),
                                                                                   TensorInfo(TensorShape(17U, 31U, 19U), 1, DataType::QASYMM8_SIGNED),
                                          })),
                                          make("ConvInfo", { PadStrideInfo(1, 2, 1, 1),
                                                                                 PadStrideInfo(1, 2, 1, 1),
                                                                                 PadStrideInfo(1, 1, 0, 0),
                                                                                 PadStrideInfo(1, 1, 0, 0),
                                                                                 PadStrideInfo(2, 1, 0, 0),
                                                                                 PadStrideInfo(3, 2, 1, 0),
                                                                                 PadStrideInfo(1, 1, 2, 2),
                                                                                 PadStrideInfo(1, 1, 2, 2),
                                                                                 PadStrideInfo(1, 1, 2, 2),
                                          })),
                                          make("GpuTarget", { GPUTarget::BIFROST,
                                                                                  GPUTarget::MIDGARD,
                                                                                  GPUTarget::G71,
                                                                                  GPUTarget::G71,
                                                                                  GPUTarget::MIDGARD,
                                                                                  GPUTarget::BIFROST,
                                                                                  GPUTarget::BIFROST,
                                                                                  GPUTarget::BIFROST,
                                                                                  GPUTarget::BIFROST,
                                          })),
                                          make("Dilation", { Size2D(1U, 1U),
                                                                 Size2D(1U, 1U),
                                                                 Size2D(1U, 1U),
                                                                 Size2D(1U, 1U),
                                                                 Size2D(1U, 1U),
                                                                 Size2D(1U, 1U),
                                                                 Size2D(1U, 1U),
                                                                 Size2D(2U, 1U),
                                                                 Size2D(2U, 1U),
                                          })),
                                         make("EnableFastMath", { false, false, false, false, false, false, true, true, true })),
                                         make("Expected",{ ConvolutionMethod::GEMM,
                                                                               ConvolutionMethod::GEMM,
                                                                               ConvolutionMethod::GEMM,
                                                                               ConvolutionMethod::WINOGRAD,
                                                                               ConvolutionMethod::GEMM,
                                                                               ConvolutionMethod::GEMM,
                                                                               ConvolutionMethod::WINOGRAD,
                                                                               ConvolutionMethod::GEMM,
                                                                               ConvolutionMethod::GEMM,
                                         })),
                                         input_info, weights_info, output_info, conv_info, gpu_target, dilation, enable_fast_math, expected)
{
    ConvolutionMethod is_valid = CLConvolutionLayer::get_convolution_method(&input_info.clone()->set_is_resizable(true),
                                                                            &weights_info.clone()->set_is_resizable(true),
                                                                            &output_info.clone()->set_is_resizable(true), conv_info,
                                                                            WeightsInfo(),
                                                                            ActivationLayerInfo(),
                                                                            gpu_target,
                                                                            dilation,
                                                                            enable_fast_math);
    ARM_COMPUTE_EXPECT(is_valid == expected, framework::LogLevel::ERRORS);
}
// clang-format on
// *INDENT-ON*
TEST_SUITE_END() // ConvolutionLayer

TEST_SUITE(GEMMConvolutionLayer)
template <typename T>
using CLGEMMConvolutionLayerFixture = ConvolutionValidationFixture<CLTensor, CLAccessor, CLGEMMConvolutionLayer, T>;
template <typename T>
using CLGEMMConvolutionLayerMixedDataLayoutFixture = ConvolutionValidationFixture<CLTensor, CLAccessor, CLGEMMConvolutionLayer, T, true>;
template <typename T>
using CLConvolutionValidationWithPaddingFixture = ConvolutionValidationWithPaddingFixture<CLTensor, CLAccessor, CLGEMMConvolutionLayer, T>;

TEST_SUITE(Float)
TEST_SUITE(FP16)

FIXTURE_DATA_TEST_CASE(RunSmall, CLGEMMConvolutionLayerFixture<half>, framework::DatasetMode::ALL, combine(combine(combine(combine(datasets::SmallConvolutionLayerDataset(),
                                                                                                                   make("ReshapeWeights", { true })), make("DataType", DataType::F16)), make("DataLayout", { DataLayout::NCHW, DataLayout::NHWC })),
                                                                                                           ActivationFunctionsSmallDataset))
{
    // Validate output
    validate(CLAccessor(_target), _reference, tolerance_f16, tolerance_num);
}
TEST_SUITE_END() // FP16

TEST_SUITE(FP32)

FIXTURE_DATA_TEST_CASE(RunSmall, CLGEMMConvolutionLayerFixture<float>, framework::DatasetMode::ALL, combine(combine(combine(combine(datasets::SmallConvolutionLayerDataset(),
                                                                                                                    make("ReshapeWeights", { true })), make("DataType", DataType::F32)), make("DataLayout", { DataLayout::NCHW, DataLayout::NHWC })),
                                                                                                            ActivationFunctionsSmallDataset))
{
    // Validate output
    validate(CLAccessor(_target), _reference, tolerance_f32);
}
FIXTURE_DATA_TEST_CASE(RunMixedDataLayout, CLGEMMConvolutionLayerMixedDataLayoutFixture<float>, framework::DatasetMode::ALL,
                       combine(combine(combine(combine(combine(combine(combine(combine(combine(
                                                                                           make("Input", TensorShape(23U, 27U, 5U)),
                                                                                           make("Weights", TensorShape(3U, 3U, 5U, 2U))),
                                                                                       make("Bias", TensorShape(2U))),
                                                                               make("Output", TensorShape(11U, 25U, 2U))),
                                                                       make("PadStrideInfo", PadStrideInfo(2, 1, 0, 0))),
                                                               make("Dilation", Size2D(1, 1))),
                                                       make("ReshapeWeights", { true })),
                                               make("DataType", DataType::F32)),
                                       make("DataLayout", { DataLayout::NCHW, DataLayout::NHWC })),
                               ActivationFunctionsSmallDataset))
{
    // Validate output
    validate(CLAccessor(_target), _reference, tolerance_f32);
}
FIXTURE_DATA_TEST_CASE(RunSmallWithPadding, CLConvolutionValidationWithPaddingFixture<float>, framework::DatasetMode::ALL,
                       combine(combine(combine(combine(combine(datasets::SmallConvolutionLayerPrePaddingDataset(),
                                                               make("ReshapeWeights", { true })),
                                                       make("DataType", DataType::F32)),
                                               make("DataLayout", { DataLayout::NCHW, DataLayout::NHWC })),
                                       make("ActivationInfo", { ActivationLayerInfo() })),
make("PrePadLayer", { PaddingList({ { 1, 1 }, { 1, 1 } }) })))
{
    // Validate output
    validate(CLAccessor(_target), _reference, tolerance_f32);
}

TEST_SUITE_END() // FP32
TEST_SUITE_END() // Float

template <typename T>
using CLGEMMConvolutionLayerQuantizedFixture = ConvolutionValidationQuantizedFixture<CLTensor, CLAccessor, CLGEMMConvolutionLayer, T>;
template <typename T>
using CLGEMMConvolutionLayerQuantizedMixedDataLayoutFixture = ConvolutionValidationQuantizedFixture<CLTensor, CLAccessor, CLGEMMConvolutionLayer, T, true>;
template <typename T>
using CLGEMMConvolutionLayerQuantizedPerChannelFixture = ConvolutionValidationQuantizedPerChannelFixture<CLTensor, CLAccessor, CLGEMMConvolutionLayer, T, int8_t>;

TEST_SUITE(Quantized)

const auto QuantizationData = make("QuantizationInfo",
{
    QuantizationInfo(0.5f, 10),
    QuantizationInfo(0.3f, 3),
    QuantizationInfo(1.1f, 10),
});

/// @note: Every asymmetric quantized test has a version with or without activation because the quantization info given
/// is ignored when there is no activation. Instead of using the same quantization information for all the tensors, the
/// fixture generates separate quantization info for each input and the output tensor.
/// When we can also support dynamic quantization with the presence of activation, these two versions should be merged
/// again, with the explicitly specified quantization info removed
const auto NoActivation = make("ActivationInfo", ActivationLayerInfo());

const auto IgnoredQuantizationInfo = make("IgnoredQuantizationInfo", QuantizationInfo());

const auto QuantizedActivationFunctionsSmallDataset = make("ActivationInfo",
{
    ActivationLayerInfo(ActivationLayerInfo::ActivationFunction::LU_BOUNDED_RELU, 6.f)
});

TEST_SUITE(QASYMM8)

FIXTURE_DATA_TEST_CASE(RunSmallCases, CLGEMMConvolutionLayerQuantizedFixture<uint8_t>, framework::DatasetMode::ALL,
    combine(SmallConvolutionLayerDatasetCases(),
        make("ReshapeWeights", { true }),
        make("DataType", DataType::QASYMM8),
        make("DataLayout", { DataLayout::NCHW, DataLayout::NHWC }),
        IgnoredQuantizationInfo,
        NoActivation))
{
    // Validate output
    validate(CLAccessor(_target), _reference, tolerance_qasymm8);
}

FIXTURE_DATA_TEST_CASE(RunSmallCasesWithActivation, CLGEMMConvolutionLayerQuantizedFixture<uint8_t>, framework::DatasetMode::ALL,
    combine(SmallConvolutionLayerDatasetCases(),
        make("ReshapeWeights", { true }),
        make("DataType", DataType::QASYMM8),
        make("DataLayout", { DataLayout::NCHW, DataLayout::NHWC }),
        QuantizationData,
        QuantizedActivationFunctionsSmallDataset))
{
    // Validate output
    validate(CLAccessor(_target), _reference, tolerance_qasymm8);
}

FIXTURE_DATA_TEST_CASE(RunSmall, CLGEMMConvolutionLayerQuantizedFixture<uint8_t>, framework::DatasetMode::ALL,
    combine(datasets::SmallConvolutionLayerDataset(),
        make("ReshapeWeights", { true }),
        make("DataType", DataType::QASYMM8),
        make("DataLayout", { DataLayout::NCHW, DataLayout::NHWC }),
        IgnoredQuantizationInfo,
        NoActivation))
{
    // Validate output
    validate(CLAccessor(_target), _reference, tolerance_qasymm8);
}

FIXTURE_DATA_TEST_CASE(RunSmallWithActivation, CLGEMMConvolutionLayerQuantizedFixture<uint8_t>, framework::DatasetMode::ALL,
    combine(datasets::SmallConvolutionLayerDataset(),
        make("ReshapeWeights", { true }),
        make("DataType", DataType::QASYMM8),
        make("DataLayout", { DataLayout::NCHW, DataLayout::NHWC }),
        QuantizationData,
        QuantizedActivationFunctionsSmallDataset))
{
    // Validate output
    validate(CLAccessor(_target), _reference, tolerance_qasymm8);
}
FIXTURE_DATA_TEST_CASE(RunMixedDataLayout, CLGEMMConvolutionLayerQuantizedMixedDataLayoutFixture<uint8_t>, framework::DatasetMode::ALL,
    combine(
        make("Input", TensorShape(23U, 27U, 5U)),
        make("Weights", TensorShape(3U, 3U, 5U, 2U)),
        make("Bias", TensorShape(2U)),
        make("Output", TensorShape(11U, 25U, 2U)),
        make("PadStrideInfo", PadStrideInfo(2, 1, 0, 0)),
        make("Dilation", Size2D(1, 1)),
        make("ReshapeWeights", { true }),
        make("DataType", DataType::QASYMM8),
        make("DataLayout", { DataLayout::NCHW, DataLayout::NHWC }),
        IgnoredQuantizationInfo,
        NoActivation))
{
    // Validate output
    validate(CLAccessor(_target), _reference, tolerance_qasymm8);
}
FIXTURE_DATA_TEST_CASE(RunMixedDataLayoutWithActivation, CLGEMMConvolutionLayerQuantizedMixedDataLayoutFixture<uint8_t>, framework::DatasetMode::ALL,
    combine(
        make("Input", TensorShape(23U, 27U, 5U)),
        make("Weights", TensorShape(3U, 3U, 5U, 2U)),
        make("Bias", TensorShape(2U)),
        make("Output", TensorShape(11U, 25U, 2U)),
        make("PadStrideInfo", PadStrideInfo(2, 1, 0, 0)),
        make("Dilation", Size2D(1, 1)),
        make("ReshapeWeights", { true }),
        make("DataType", DataType::QASYMM8),
        make("DataLayout", { DataLayout::NCHW, DataLayout::NHWC }),
        QuantizationData,
        QuantizedActivationFunctionsSmallDataset))
{
    // Validate output
    validate(CLAccessor(_target), _reference, tolerance_qasymm8);
}
TEST_SUITE_END() // QASYMM8
TEST_SUITE(QASYMM8_SIGNED)
FIXTURE_DATA_TEST_CASE(RunSmall, CLGEMMConvolutionLayerQuantizedFixture<int8_t>, framework::DatasetMode::ALL,
    combine(datasets::SmallConvolutionLayerDataset(),
        make("ReshapeWeights", { true }),
        make("DataType", DataType::QASYMM8_SIGNED),
        make("DataLayout", { DataLayout::NCHW, DataLayout::NHWC }),
        IgnoredQuantizationInfo,
        NoActivation))
{
    // Validate output
    validate(CLAccessor(_target), _reference, tolerance_qasymm8);
}
FIXTURE_DATA_TEST_CASE(RunSmallWithActivation, CLGEMMConvolutionLayerQuantizedFixture<int8_t>, framework::DatasetMode::ALL,
    combine(datasets::SmallConvolutionLayerDataset(),
        make("ReshapeWeights", { true }),
        make("DataType", DataType::QASYMM8_SIGNED),
        make("DataLayout", { DataLayout::NCHW, DataLayout::NHWC }),
        QuantizationData,
        QuantizedActivationFunctionsSmallDataset))
{
    // Validate output
    validate(CLAccessor(_target), _reference, tolerance_qasymm8);
}
FIXTURE_DATA_TEST_CASE(RunMixedDataLayout, CLGEMMConvolutionLayerQuantizedMixedDataLayoutFixture<int8_t>, framework::DatasetMode::ALL,
    combine(
        make("Input", TensorShape(23U, 27U, 5U)),
        make("Weights", TensorShape(3U, 3U, 5U, 2U)),
        make("Bias", TensorShape(2U)),
        make("Output", TensorShape(11U, 25U, 2U)),
        make("PadStrideInfo", PadStrideInfo(2, 1, 0, 0)),
        make("Dilation", Size2D(1, 1)),
        make("ReshapeWeights", { true }),
        make("DataType", DataType::QASYMM8_SIGNED),
        make("DataLayout", { DataLayout::NCHW, DataLayout::NHWC }),
        IgnoredQuantizationInfo,
        NoActivation))
{
    // Validate output
    validate(CLAccessor(_target), _reference, tolerance_qasymm8);
}
FIXTURE_DATA_TEST_CASE(RunMixedDataLayoutWithActivation, CLGEMMConvolutionLayerQuantizedMixedDataLayoutFixture<int8_t>, framework::DatasetMode::ALL,
    combine(
        make("Input", TensorShape(23U, 27U, 5U)),
        make("Weights", TensorShape(3U, 3U, 5U, 2U)),
        make("Bias", TensorShape(2U)),
        make("Output", TensorShape(11U, 25U, 2U)),
        make("PadStrideInfo", PadStrideInfo(2, 1, 0, 0)),
        make("Dilation", Size2D(1, 1)),
        make("ReshapeWeights", { true }),
        make("DataType", DataType::QASYMM8_SIGNED),
        make("DataLayout", { DataLayout::NCHW, DataLayout::NHWC }),
        QuantizationData,
        QuantizedActivationFunctionsSmallDataset))
{
    // Validate output
    validate(CLAccessor(_target), _reference, tolerance_qasymm8);
}
TEST_SUITE_END() // QASYMM8_SIGNED
TEST_SUITE(QSYMM8_PER_CHANNEL)
const auto QuantizedActivationFunctionsSmallPerChannelDataset = make("ActivationInfo",
{
    ActivationLayerInfo(),
    ActivationLayerInfo(ActivationLayerInfo::ActivationFunction::LU_BOUNDED_RELU, 6.f)
});


FIXTURE_DATA_TEST_CASE(RunSmallSigned, CLGEMMConvolutionLayerQuantizedPerChannelFixture<int8_t>, framework::DatasetMode::ALL,
                       combine(combine(combine(combine(combine(combine(datasets::SmallConvolutionLayerDataset(),
                                                                       make("ReshapeWeights", { true })),
                                                               make("DataType", { DataType::QASYMM8_SIGNED })),
                                                       make("DataLayout", { DataLayout::NCHW, DataLayout::NHWC })),
                                               QuantizationData),
                                       QuantizedActivationFunctionsSmallPerChannelDataset),
                               make("WeightsDataType", { DataType::QSYMM8_PER_CHANNEL })))
{
    // Validate output
    validate(CLAccessor(_target), _reference, tolerance_qasymm8);
}

FIXTURE_DATA_TEST_CASE(RunSmall, CLGEMMConvolutionLayerQuantizedPerChannelFixture<uint8_t>, framework::DatasetMode::ALL,
                       combine(combine(combine(combine(combine(combine(datasets::SmallConvolutionLayerDataset(),
                                                                       make("ReshapeWeights", { true })),
                                                               make("DataType", { DataType::QASYMM8 })),
                                                       make("DataLayout", { DataLayout::NCHW, DataLayout::NHWC })),
                                               QuantizationData),
                                       QuantizedActivationFunctionsSmallPerChannelDataset),
                               make("WeightsDataType", { DataType::QSYMM8_PER_CHANNEL })))
{
    // Validate output
    validate(CLAccessor(_target), _reference, tolerance_qasymm8);
}
TEST_SUITE_END() // QSYMM8_PER_CHANNEL
TEST_SUITE_END() // Quantized

TEST_SUITE_END() // GEMMConvolutionLayer

template <typename T>
using CLGEMMGroupedConvolutionLayerFixture = ConvolutionValidationFixture<CLTensor, CLAccessor, CLGEMMConvolutionLayer, T>;

TEST_SUITE(GroupedGEMMConvolutionLayer)

TEST_SUITE(Float)
TEST_SUITE(FP32)

FIXTURE_DATA_TEST_CASE(RunSmall, CLGEMMGroupedConvolutionLayerFixture<float>, framework::DatasetMode::ALL, combine(combine(combine(combine(datasets::SmallGroupedConvolutionLayerDataset(),
                                                                                                                   make("ReshapeWeights", { true })), make("DataType", DataType::F32)), make("DataLayout", { DataLayout::NCHW })),
                                                                                                                   ActivationFunctionsSmallDataset))
{
    // Validate output
    validate(CLAccessor(_target), _reference, tolerance_f32, tolerance_num);
}

FIXTURE_DATA_TEST_CASE(RunLarge, CLGEMMGroupedConvolutionLayerFixture<float>, framework::DatasetMode::NIGHTLY,
                       combine(combine(combine(combine(datasets::LargeGroupedConvolutionLayerDataset(),
                                                       make("ReshapeWeights", { true })),
                                               make("DataType", DataType::F32)),
                                       make("DataLayout", { DataLayout::NCHW })),
                               ActivationFunctionsDataset))
{
    // Validate output
    validate(CLAccessor(_target), _reference, tolerance_f32, tolerance_num);
}
TEST_SUITE_END() // FP32

TEST_SUITE(FP16)

FIXTURE_DATA_TEST_CASE(RunSmall, CLGEMMGroupedConvolutionLayerFixture<half>, framework::DatasetMode::ALL, combine(combine(combine(combine(datasets::SmallGroupedConvolutionLayerDataset(),
                                                                                                                  make("ReshapeWeights", { true })), make("DataType", DataType::F16)), make("DataLayout", { DataLayout::NCHW })),
                                                                                                                  ActivationFunctionsSmallDataset))
{
    // Validate output
    validate(CLAccessor(_target), _reference, tolerance_f32, tolerance_num);
}

FIXTURE_DATA_TEST_CASE(RunLarge, CLGEMMGroupedConvolutionLayerFixture<half>, framework::DatasetMode::NIGHTLY,
                       combine(combine(combine(combine(datasets::LargeGroupedConvolutionLayerDataset(),
                                                       make("ReshapeWeights", { true })),
                                               make("DataType", DataType::F16)),
                                       make("DataLayout", { DataLayout::NCHW })),
                               ActivationFunctionsDataset))
{
    // Validate output
    validate(CLAccessor(_target), _reference, tolerance_f32, tolerance_num);
}
TEST_SUITE_END() // FP16
TEST_SUITE_END() // Float

TEST_SUITE_END() // GroupedGEMMConvolutionLayer
TEST_SUITE_END() // CL
} // namespace validation
} // namespace test
} // namespace arm_compute
