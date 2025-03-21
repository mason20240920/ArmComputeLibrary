/*
 * Copyright (c) 2018-2024 Arm Limited.
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
#include "arm_compute/runtime/NEON/functions/NEReduceMean.h"

#include "arm_compute/core/Error.h"
#include "arm_compute/core/utils/misc/ShapeCalculator.h"

#include "src/common/utils/Log.h"
#include "src/core/CPP/Validate.h"
#include "src/core/helpers/AutoConfiguration.h"
#include "src/core/NEON/kernels/NEReductionOperationKernel.h"

namespace arm_compute
{
namespace
{
Status
validate_config(const ITensorInfo *input, const Coordinates &reduction_axis, bool keep_dims, const ITensorInfo *output)
{
    ARM_COMPUTE_UNUSED(keep_dims);
    ARM_COMPUTE_RETURN_ERROR_ON_NULLPTR(input, output);
    ARM_COMPUTE_RETURN_ERROR_ON_DYNAMIC_SHAPE(input, output);
    ARM_COMPUTE_RETURN_ERROR_ON_CPU_F16_UNSUPPORTED(input);
    ARM_COMPUTE_RETURN_ERROR_ON_DATA_TYPE_CHANNEL_NOT_IN(input, 1, DataType::QASYMM8_SIGNED, DataType::QASYMM8,
                                                         DataType::F16, DataType::F32);
    ARM_COMPUTE_RETURN_ERROR_ON(reduction_axis.num_dimensions() < 1);
    ARM_COMPUTE_RETURN_ERROR_ON(reduction_axis.num_dimensions() > input->num_dimensions());

    const unsigned int reduction_ops = reduction_axis.num_dimensions();
    const int          input_dims    = input->num_dimensions();
    Coordinates        axis_local    = reduction_axis;

    for (unsigned int i = 0; i < axis_local.num_dimensions(); ++i)
    {
        //axis: The dimensions to reduce. Must be in the range [-rank(input_tensor), rank(input_tensor)).
        ARM_COMPUTE_RETURN_ERROR_ON(axis_local[i] < (-static_cast<int>(input->num_dimensions())));
        ARM_COMPUTE_RETURN_ERROR_ON(axis_local[i] >= static_cast<int>(input->num_dimensions()));
    }

    if (output->tensor_shape().total_size() != 0)
    {
        // Only validate if not using auto_init for the output tensor
        TensorShape out_shape = input->tensor_shape();
        // Validate output_shape only if not using auto_init
        convert_negative_axis(axis_local, input_dims);

// Suppress warning produced by a compiler bug in GCC
// https://gcc.gnu.org/bugzilla/show_bug.cgi?id=104165
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
        std::sort(axis_local.begin(), axis_local.begin() + reduction_ops);
#pragma GCC diagnostic pop

        for (unsigned int i = 0; i < reduction_ops; ++i)
        {
            ARM_COMPUTE_RETURN_ERROR_ON(axis_local[i] > 3);
            ARM_COMPUTE_RETURN_ERROR_ON(static_cast<unsigned int>(axis_local[i]) > input->num_dimensions() - 1);
            if (output->total_size() > 0 && keep_dims)
            {
                ARM_COMPUTE_RETURN_ERROR_ON(output->dimension(axis_local[i]) != 1);
            }
            if (keep_dims)
            {
                out_shape.set(axis_local[i], 1);
            }
            else
            {
                ARM_COMPUTE_RETURN_ERROR_ON(i > static_cast<unsigned int>(axis_local[i]));
                const unsigned int remove_index = axis_local[i] - i;
                ARM_COMPUTE_RETURN_ERROR_ON(remove_index >= out_shape.num_dimensions());
                out_shape.remove_dimension(remove_index, false);
            }
        }
        const TensorInfo out_info = input->clone()->set_tensor_shape(out_shape);
        ARM_COMPUTE_RETURN_ERROR_ON_MISMATCHING_SHAPES(output, &out_info);
    }
    return Status{};
}
} // namespace

NEReduceMean::~NEReduceMean() = default;

NEReduceMean::NEReduceMean(std::shared_ptr<IMemoryManager> memory_manager)
    : _memory_group(std::move(memory_manager)),
      _reduction_kernels(),
      _reduced_outs(),
      _reshape(),
      _reduction_ops(),
      _keep_dims()
{
}

Status NEReduceMean::validate(const ITensorInfo *input,
                              const Coordinates &reduction_axis,
                              bool               keep_dims,
                              const ITensorInfo *output)
{
    return validate_config(input, reduction_axis, keep_dims, output);
}

void NEReduceMean::configure(ITensor *input, const Coordinates &reduction_axis, bool keep_dims, ITensor *output)
{
    ARM_COMPUTE_LOG_PARAMS(input, reduction_axis, keep_dims, output);

    // Perform validate step
    ARM_COMPUTE_ERROR_THROW_ON(NEReduceMean::validate(input->info(), reduction_axis, keep_dims, output->info()));
    // Output auto inizialitation if not yet initialized
    const TensorShape output_shape =
        arm_compute::misc::shape_calculator::calculate_reduce_mean_shape(input->info(), reduction_axis, keep_dims);
    auto_init_if_empty(*output->info(), input->info()->clone()->set_tensor_shape(output_shape));

    _reduction_ops = reduction_axis.num_dimensions();
    _reduction_kernels.resize(_reduction_ops);
    _reduced_outs.resize(_reduction_ops - (keep_dims ? 1 : 0));
    _keep_dims = keep_dims;

    ITensor *tmp_input  = input;
    ITensor *tmp_output = output;

    Coordinates axis_local = reduction_axis;
    const int   input_dims = tmp_input->info()->num_dimensions();

    convert_negative_axis(axis_local, input_dims);

    // Perform reduction for every axis
    for (int i = 0; i < _reduction_ops; ++i)
    {
        TensorShape out_shape =
            i == 0 ? tmp_input->info()->tensor_shape() : (&_reduced_outs[i - 1])->info()->tensor_shape();
        out_shape.set(axis_local[i], 1);
        auto in = (i == 0) ? tmp_input : (&_reduced_outs[i - 1]);

        if (i == _reduction_ops - 1 && keep_dims)
        {
            _reduction_kernels[i].configure(in, tmp_output, axis_local[i], ReductionOperation::MEAN_SUM);
        }
        else
        {
            _reduced_outs[i].allocator()->init(TensorInfo(out_shape, tmp_output->info()->num_channels(),
                                                          tmp_output->info()->data_type(),
                                                          tmp_output->info()->quantization_info()));
            _memory_group.manage(&_reduced_outs[i]);
            _reduction_kernels[i].configure(in, &_reduced_outs[i], axis_local[i], ReductionOperation::MEAN_SUM);
        }
    }

    // Allocate intermediate tensors
    for (int i = 0; i < _reduction_ops - (keep_dims ? 1 : 0); ++i)
    {
        _reduced_outs[i].allocator()->allocate();
    }
    // Configure reshape layer if we want to drop the dimensions
    if (!keep_dims)
    {
        TensorShape out_shape = tmp_input->info()->tensor_shape();
        // We have to sort the reduction axis vectors in order for remove_dimension
        // to work properly

// Suppress warning produced by a compiler bug in GCC
// https://gcc.gnu.org/bugzilla/show_bug.cgi?id=104165
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
        std::sort(axis_local.begin(), axis_local.begin() + _reduction_ops);
#pragma GCC diagnostic pop

        for (int i = 0; i < _reduction_ops; ++i)
        {
            out_shape.remove_dimension(axis_local[i] - i, false);
        }
        auto_init_if_empty(*tmp_output->info(), tmp_input->info()->clone()->set_tensor_shape(out_shape));
        _reshape.configure(&_reduced_outs[_reduction_ops - 1], tmp_output);
    }
}

void NEReduceMean::run()
{
    MemoryGroupResourceScope scope_mg(_memory_group);
    for (auto &kernel : _reduction_kernels)
    {
        kernel.run();
    }
    if (!_keep_dims)
    {
        _reshape.run();
    }
}
} // namespace arm_compute
