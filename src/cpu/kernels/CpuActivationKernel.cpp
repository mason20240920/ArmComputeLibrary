/*
 * Copyright (c) 2017-2025 Arm Limited.
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
#include "src/cpu/kernels/CpuActivationKernel.h"

#include "arm_compute/core/ITensor.h"
#include "arm_compute/core/TensorInfo.h"
#include "arm_compute/core/Utils.h"

#include "src/core/common/Registrars.h"
#include "src/core/CPP/Validate.h"
#include "src/core/helpers/AutoConfiguration.h"
#include "src/core/helpers/WindowHelpers.h"
#include "src/cpu/kernels/activation/heuristics/CpuActivationKernelHeuristics.h"
#include "src/cpu/kernels/activation/list.h"
#include "src/cpu/kernels/logistic/list.h"

#include <array>

namespace arm_compute
{
namespace cpu
{
namespace kernels
{
namespace
{

/* Supported activation in the 8-bit integer domain */
static const std::array<ActivationLayerInfo::ActivationFunction, 8> qasymm8_activations = {
    ActivationLayerInfo::ActivationFunction::RELU,         ActivationLayerInfo::ActivationFunction::LU_BOUNDED_RELU,
    ActivationLayerInfo::ActivationFunction::BOUNDED_RELU, ActivationLayerInfo::ActivationFunction::LOGISTIC,
    ActivationLayerInfo::ActivationFunction::TANH,         ActivationLayerInfo::ActivationFunction::HARD_SWISH,
    ActivationLayerInfo::ActivationFunction::LEAKY_RELU,   ActivationLayerInfo::ActivationFunction::GELU,
};

/* Static quantization can only, currently, support relu based activations */
static const std::array<ActivationLayerInfo::ActivationFunction, 3> qasymm8_static_quant_activations = {
    ActivationLayerInfo::ActivationFunction::RELU, ActivationLayerInfo::ActivationFunction::BOUNDED_RELU,
    ActivationLayerInfo::ActivationFunction::LU_BOUNDED_RELU};

/* Supported activation in the 16-bit integer domain */
static const std::array<ActivationLayerInfo::ActivationFunction, 4> qsymm16_activations = {
    ActivationLayerInfo::ActivationFunction::LOGISTIC, ActivationLayerInfo::ActivationFunction::TANH,
    ActivationLayerInfo::ActivationFunction::HARD_SWISH, ActivationLayerInfo::ActivationFunction::LU_BOUNDED_RELU};

Status validate_arguments(const ITensorInfo *src, const ITensorInfo *dst, const ActivationLayerInfo &activation_info)
{
    ARM_COMPUTE_RETURN_ERROR_ON_CPU_F16_UNSUPPORTED(src);
    ARM_COMPUTE_RETURN_ERROR_ON_DATA_TYPE_CHANNEL_NOT_IN(src, 1, DataType::QASYMM8_SIGNED, DataType::QASYMM8,
                                                         DataType::QSYMM16, DataType::F16, DataType::F32);

    heuristics::CpuActivationKernelHeuristics heuristics(src, dst, activation_info);
    const auto                               *uk = heuristics.kernel();
    ARM_COMPUTE_RETURN_ERROR_ON(uk == nullptr || uk->ukernel == nullptr);

    const DataType          data_type = src->data_type();
    const QuantizationInfo &oq_info   = (dst != nullptr) ? dst->quantization_info() : src->quantization_info();
    const ActivationLayerInfo::ActivationFunction f_act = activation_info.activation();

    ARM_COMPUTE_RETURN_ERROR_ON_MSG(
        is_data_type_quantized_asymmetric_char(data_type) && oq_info.is_dynamic() &&
            (std::find(std::begin(qasymm8_static_quant_activations), std::end(qasymm8_static_quant_activations),
                       f_act) == std::end(qasymm8_static_quant_activations)),
        "For QASYMM8 statically quantized, only relu and lower/upper bounded relu are supported");

    ARM_COMPUTE_RETURN_ERROR_ON_MSG(
        is_data_type_quantized_asymmetric(data_type) &&
            (std::find(std::begin(qasymm8_activations), std::end(qasymm8_activations), f_act) ==
             std::end(qasymm8_activations)),
        "For QASYMM8 only hard swish, leaky relu, tanh, logistic, relu and lower/upper bounded relu are supported");

    ARM_COMPUTE_RETURN_ERROR_ON_MSG(is_data_type_quantized_symmetric(data_type) &&
                                        (std::find(std::begin(qsymm16_activations), std::end(qsymm16_activations),
                                                   f_act) == std::end(qsymm16_activations)),
                                    "For QSYMM16 only tanh and logistic are supported");
    ARM_COMPUTE_RETURN_ERROR_ON((data_type == DataType::QASYMM8 || data_type == DataType::QASYMM16) &&
                                (f_act == ActivationLayerInfo::ActivationFunction::TANH) &&
                                (oq_info != QuantizationInfo(1.f / 128.f, 128)));
    ARM_COMPUTE_RETURN_ERROR_ON((data_type == DataType::QASYMM8 || data_type == DataType::QASYMM16) &&
                                (f_act == ActivationLayerInfo::ActivationFunction::LOGISTIC) &&
                                (oq_info != QuantizationInfo(1.f / 256.f, 0)));

    ARM_COMPUTE_RETURN_ERROR_ON(data_type == DataType::QASYMM8_SIGNED &&
                                (f_act == ActivationLayerInfo::ActivationFunction::TANH) &&
                                (oq_info != QuantizationInfo(1.f / 128.f, 0)));
    ARM_COMPUTE_RETURN_ERROR_ON(data_type == DataType::QASYMM8_SIGNED &&
                                (f_act == ActivationLayerInfo::ActivationFunction::LOGISTIC) &&
                                (oq_info != QuantizationInfo(1.f / 256.f, -128)));

    ARM_COMPUTE_RETURN_ERROR_ON(is_data_type_quantized_symmetric(data_type) &&
                                (f_act == ActivationLayerInfo::ActivationFunction::TANH) &&
                                (oq_info != QuantizationInfo(1.f / 32768.f, 0)));
    ARM_COMPUTE_RETURN_ERROR_ON(is_data_type_quantized_symmetric(data_type) &&
                                (f_act == ActivationLayerInfo::ActivationFunction::LOGISTIC) &&
                                (oq_info != QuantizationInfo(1.f / 32768.f, 0)));

    // Checks performed when dst is configured
    if ((dst != nullptr) && (dst->total_size() != 0))
    {
        ARM_COMPUTE_RETURN_ERROR_ON_MISMATCHING_SHAPES(src, dst);
        ARM_COMPUTE_RETURN_ERROR_ON_MISMATCHING_DATA_TYPES(src, dst);
    }

    return Status{};
}

#ifdef __aarch64__
// TODO (COMPMID-7511): delegate to LUTManager
void init_lut(ActivationLayerInfo::ActivationFunction act_func,
              DataType                                data_type,
              const UniformQuantizationInfo          &qi_in,
              const UniformQuantizationInfo          &qi_out,
              ActivationLayerInfo::LookupTable256    &lut,
              float                                   a,
              float                                   b)
{
    for (size_t i = 0; i < lut.size(); ++i)
    {
        float tmp_f =
            (data_type == DataType::QASYMM8) ? dequantize_qasymm8(i, qi_in) : dequantize_qasymm8_signed(i, qi_in);
        switch (act_func)
        {
            case ActivationLayerInfo::ActivationFunction::HARD_SWISH:
                tmp_f = tmp_f * ((std::min(std::max((tmp_f + 3), 0.0f), 6.0f)) * 0.166666667f);
                break;
            case ActivationLayerInfo::ActivationFunction::LEAKY_RELU:
                tmp_f = tmp_f > 0 ? tmp_f : tmp_f * a;
                break;
            case ActivationLayerInfo::ActivationFunction::LOGISTIC:
                tmp_f = 1.f / (1.f + std::exp(-tmp_f));
                break;
            case ActivationLayerInfo::ActivationFunction::ABS:
                tmp_f = std::abs(tmp_f);
                break;
            case ActivationLayerInfo::ActivationFunction::LINEAR:
                tmp_f = a * tmp_f + b;
                break;
            case ActivationLayerInfo::ActivationFunction::BOUNDED_RELU:
                tmp_f = std::min<>(a, std::max(0.f, tmp_f));
                break;
            case ActivationLayerInfo::ActivationFunction::LU_BOUNDED_RELU:
                tmp_f = std::min<>(a, std::max<>(b, tmp_f));
                break;
            case ActivationLayerInfo::ActivationFunction::SOFT_RELU:
                tmp_f = (tmp_f > 12.f) ? tmp_f : std::log(1.f + std::exp(tmp_f));
                break;
            case ActivationLayerInfo::ActivationFunction::ELU:
                tmp_f = (tmp_f >= 0) ? tmp_f : a * (std::exp(tmp_f) - 1);
                break;
            case ActivationLayerInfo::ActivationFunction::SQRT:
                tmp_f = std::sqrt(tmp_f);
                break;
            case ActivationLayerInfo::ActivationFunction::SQUARE:
                tmp_f = tmp_f * tmp_f;
                break;
            case ActivationLayerInfo::ActivationFunction::TANH:
                tmp_f = a * std::tanh(b * tmp_f);
                break;
            case ActivationLayerInfo::ActivationFunction::IDENTITY:
                break;
            case ActivationLayerInfo::ActivationFunction::SWISH:
                tmp_f = tmp_f / (1.f + std::exp(-a * tmp_f));
                break;
            case ActivationLayerInfo::ActivationFunction::GELU:
                tmp_f = tmp_f * (0.5f * (1.0f + erff(tmp_f / 1.41421356237f)));
                break;
            default:
                ARM_COMPUTE_ERROR("Not supported");
                tmp_f = 0;
                break;
        }
        lut[i] =
            (data_type == DataType::QASYMM8) ? quantize_qasymm8(tmp_f, qi_out) : quantize_qasymm8_signed(tmp_f, qi_out);
    }
}
#endif // __aarch64__
} // namespace

void CpuActivationKernel::configure(const ITensorInfo *src, ITensorInfo *dst, ActivationLayerInfo activation_info)
{
    ARM_COMPUTE_UNUSED(dst);
    ARM_COMPUTE_ERROR_ON_NULLPTR(src);
    ARM_COMPUTE_ERROR_THROW_ON(CpuActivationKernel::validate(src, dst, activation_info));

    heuristics::CpuActivationKernelHeuristics heuristics(src, dst, activation_info);
    _heuristics = std::move(heuristics);

    if (dst != nullptr)
    {
        // dst auto inizialitation if not yet initialized
        auto_init_if_empty(*dst, *src->clone());
    }

    const auto *uk = _heuristics.kernel();
    ARM_COMPUTE_ERROR_ON_NULLPTR(uk);

    _name = std::string("CpuActivationKernel").append("/").append(uk->name);

#ifdef __aarch64__
    // Initialise lut_manager
    LUTManager &lut_manager = LUTManager::get_instance();

    // TODO (COMPMID-7511): delegate to LUTManager
    if ((src->data_type() == DataType::QASYMM8 || src->data_type() == DataType::QASYMM8_SIGNED) &&
        activation_info.activation() != ActivationFunction::RELU)
    {
        ActivationLayerInfo::LookupTable256 tmp_lut;
        init_lut(activation_info.activation(), src->data_type(), src->quantization_info().uniform(),
                 (dst) ? dst->quantization_info().uniform() : src->quantization_info().uniform(), tmp_lut,
                 activation_info.a(), activation_info.b());
        activation_info.setLookupTable256(tmp_lut);
    }

    if (std::string(uk->name) == "sve_fp16_activation_lut")
    {
        // Create info using init list.
        const LUTInfo info = {activation_info.activation(), activation_info.a(), activation_info.b(), src->data_type(),
                              src->quantization_info().uniform()};
        activation_info.setLookupTable65536((lut_manager.get_lut_table<LookupTable65536>(info)));
    }
#endif // __aarch64__
    _act_info = activation_info;

    ICPPKernel::configure(_heuristics.window());
}

Status
CpuActivationKernel::validate(const ITensorInfo *src, const ITensorInfo *dst, const ActivationLayerInfo &act_info)
{
    ARM_COMPUTE_UNUSED(act_info);
    ARM_COMPUTE_RETURN_ON_ERROR(validate_arguments(src, dst, act_info));

    return Status{};
}

size_t CpuActivationKernel::get_mws(const CPUInfo &platform, size_t thread_count) const
{
    ARM_COMPUTE_UNUSED(thread_count);
    ARM_COMPUTE_UNUSED(platform);

    return _heuristics.mws();
}

void CpuActivationKernel::run_op(ITensorPack &tensors, const Window &window, const ThreadInfo &info)
{
    // Early exit on disabled activation
    if (!_act_info.enabled())
    {
        return;
    }

    ARM_COMPUTE_UNUSED(info);
    ARM_COMPUTE_ERROR_ON_UNCONFIGURED_KERNEL(this);
    ARM_COMPUTE_ERROR_ON_INVALID_SUBWINDOW(IKernel::window(), window);

    ARM_COMPUTE_ERROR_ON(tensors.empty());

    ActivationKernelPtr run_method = _heuristics.kernel()->ukernel;
    ARM_COMPUTE_ERROR_ON(run_method == nullptr);

    const ITensor *src = tensors.get_const_tensor(TensorType::ACL_SRC);
    ITensor       *dst = tensors.get_tensor(TensorType::ACL_DST);

    run_method(src, dst, _act_info, window);
}

const char *CpuActivationKernel::name() const
{
    return _name.c_str();
}
} // namespace kernels
} // namespace cpu
} // namespace arm_compute
