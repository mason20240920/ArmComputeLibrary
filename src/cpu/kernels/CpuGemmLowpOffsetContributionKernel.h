/*
 * Copyright (c) 2017-2022,2024-2025 Arm Limited.
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
#ifndef ACL_SRC_CPU_KERNELS_CPUGEMMLOWPOFFSETCONTRIBUTIONKERNEL_H
#define ACL_SRC_CPU_KERNELS_CPUGEMMLOWPOFFSETCONTRIBUTIONKERNEL_H

#include "src/core/common/Macros.h"
#include "src/cpu/ICpuKernel.h"

#include <cstdint>

namespace arm_compute
{
namespace cpu
{
namespace kernels
{
/** Kernel used to add the offset contribution after @ref CpuGemmLowpMatrixMultiplyKernel. The computation is performed in-place
 *
 * This kernel takes a final int32 accumulator value (the output of @ref CpuGemmLowpMatrixMultiplyKernel),
 * and adds to it the offset contribution of matrix A and matrix B in-place.
 *
 * The final result is:
 *
 * mm_result[i][k] = mm_result[i][k] +
 *                   (vector_sum_col[k] * a_offset) +
 *                   (vector_sum_row[i] * b_offset) +
 *                   (a_offset * b_offset * k)
 *
 */
class CpuGemmLowpOffsetContributionKernel : public ICpuKernel<CpuGemmLowpOffsetContributionKernel>
{
public:
    /** Default constructor */
    CpuGemmLowpOffsetContributionKernel() = default;
    ARM_COMPUTE_DISALLOW_COPY_ALLOW_MOVE(CpuGemmLowpOffsetContributionKernel);
    /** Initialise the kernel's input and output.
     *
     * @param[in, out] mm_result      Input tensor containing the result of @ref CpuGemmLowpMatrixMultiplyKernel. Data type supported: S32
     * @param[in]      vector_sum_col Input row-vector of sums of all the entries in each column of matrix B.
     *                                Note: vector_sum_col can be a nullptr in case a_offset = 0. Data type supported: same as @p mm_result
     * @param[in]      vector_sum_row Input row-vector of sums of all the entries in each row of matrix A.
     *                                Note: vector_sum_row can be a nullptr in case b_offset = 0. Data type supported: same as @p mm_result
     * @param[in]      k              Number of matrix A columns or Matrix B rows
     * @param[in]      a_offset       Offset to be added to each element of the matrix A.
     * @param[in]      b_offset       Offset to be added to each element of the matrix B.
     * @param[in]      scale          (Optional) multiplies the contribution to make it the same scale as the dst in the case where mm_result is float
     *                                (and so has already been scaled). Default is 1.0
     */
    void configure(ITensorInfo *mm_result,
                   ITensorInfo *vector_sum_col,
                   ITensorInfo *vector_sum_row,
                   int32_t      k,
                   int32_t      a_offset,
                   int32_t      b_offset,
                   float        scale = 1.0f);
    /** Static function to check if given info will lead to a valid configuration
     *
     * Similar to CpuGemmLowpOffsetContributionKernel::configure()
     *
     * @return a status
     */
    static Status validate(const ITensorInfo *mm_result,
                           const ITensorInfo *vector_sum_col,
                           const ITensorInfo *vector_sum_row,
                           int32_t            a_offset,
                           int32_t            b_offset);

    /** Set the a offset
     * Warning: if a_offset is non-zero then vector_sum_col must be set in run_op.
     *          Run configure or validate again if you aren't sure
     *
     * @param[in] a_offset Offset to be added to each element of the matrix A.
     */
    void set_a_offset(int32_t a_offset);

    /** Set the b offset
     * Warning: if b_offset is non-zero then vector_sum_row must be set in run_op.
     *          Run configure or validate again if you aren't sure
     *
     * @param[in] b_offset Offset to be added to each element of the matrix B.
     */
    void set_b_offset(int32_t b_offset);

    /** Set the dequantize scale
     *
     * @param[in] scale Multiplies the contribution to make it the same scale as the dst in the case where
     *                  mm_result is float (and so has already been scaled).
     */
    void set_scale(float scale);

    // Inherited methods overridden:
    void        run_op(ITensorPack &tensors, const Window &window, const ThreadInfo &info) override;
    const char *name() const override;

private:
    using OffsetContributionFunction = void (*)(const Window  &window,
                                                ITensor       *mm_result,
                                                const ITensor *vector_sum_col,
                                                const ITensor *vector_sum_row,
                                                int32_t        a_offset,
                                                int32_t        b_offset,
                                                int32_t        k_offset,
                                                float          scale,
                                                bool           slide_vector_sum_col,
                                                bool           is_gemm3d);

    OffsetContributionFunction _func{nullptr};
    int32_t                    _a_offset{0};
    int32_t                    _b_offset{0};
    int32_t                    _k{0}; // Number of columns of A or rows of B, used in last offset term
    float                      _scale{1.0};
    bool                       _slide_vector_sum_col{true};
};
} // namespace kernels
} // namespace cpu
} // namespace arm_compute
#endif // ACL_SRC_CPU_KERNELS_CPUGEMMLOWPOFFSETCONTRIBUTIONKERNEL_H
