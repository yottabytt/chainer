#include "xchainer/cuda/cuda_device.h"

#include <cublas_v2.h>

#include <cassert>
#include <cstdint>

#include <cuda_runtime.h>

#include "xchainer/array.h"
#include "xchainer/backend_util.h"
#include "xchainer/cuda/cublas.h"
#include "xchainer/cuda/cuda_runtime.h"
#include "xchainer/device.h"
#include "xchainer/dtype.h"
#include "xchainer/error.h"
#include "xchainer/routines/creation.h"

namespace xchainer {
namespace cuda {

namespace {

// Dispatch gemm routines based on the element type T
template <typename T>
struct Gemm;

template <>
struct Gemm<float> {
    template <typename... Args>
    void operator()(Args&&... args) const {
        CheckCublasError(cublasSgemm(std::forward<Args>(args)...));
    }
};

template <>
struct Gemm<double> {
    template <typename... Args>
    void operator()(Args&&... args) const {
        CheckCublasError(cublasDgemm(std::forward<Args>(args)...));
    }
};

struct GemmInputLayout {
    int64_t ld = 0;
    cublasOperation_t trans = CUBLAS_OP_T;

    // Makes the array C or Fortran contiguous and configure leading dimension and transposition accordingly.
    Array Configure(const Array& a) {
        assert(a.ndim() == 2);
        if (a.strides()[0] == a.item_size() && a.strides()[0] * a.shape()[0] == a.strides()[1]) {
            // Fortran contiguous
            ld = a.shape()[0];
            return a;
        }
        // Force C contiguous
        ld = a.shape()[1];
        trans = CUBLAS_OP_N;  // transposed
        return a.IsContiguous() ? a : a.AsConstant(CopyKind::kCopy);
    }
};

}  // namespace

void CudaDevice::Dot(const Array& a, const Array& b, const Array& out) {
    CheckDevicesCompatible(a, b, out);
    CheckCudaError(cudaSetDevice(index()));

    assert(a.ndim() == 2);
    assert(b.ndim() == 2);
    assert(out.ndim() == 2);

    int64_t m = a.shape()[0];
    int64_t k = a.shape()[1];
    int64_t n = b.shape()[1];
    assert(b.shape()[0] == k);
    assert(out.shape()[0] == m);
    assert(out.shape()[1] == n);

    if (m == 1 && n == 1) {
        // TODO(beam2d): Write a custom reduction kernel.
        Array l = a.AsConstant();
        Array r = b.AsConstant();
        Array o = out.AsConstant();
        Sum(l.Reshape({k}) * r.Reshape({k}), {0}, o.Reshape({}));
        return;
    }

    bool is_out_contiguous = out.IsContiguous();
    Array out_contiguous = is_out_contiguous ? out : EmptyLike(out, *this);

    auto gemm_impl = [&](auto pt) {
        using T = typename decltype(pt)::type;

        // Note that cuBLAS uses Fortran order.
        // To compute out = a x b, we use cuBLAS to compute out^T = b^T x a^T (here x is the matrix product).

        GemmInputLayout a_layout;
        GemmInputLayout b_layout;
        Array a_config = a_layout.Configure(a);
        Array b_config = b_layout.Configure(b);

        const T one = 1;
        const T zero = 0;
        const T* a_ptr = xchainer::internal::GetRawOffsetData<const T>(a_config);
        const T* b_ptr = xchainer::internal::GetRawOffsetData<const T>(b_config);
        T* out_ptr = xchainer::internal::GetRawOffsetData<T>(out_contiguous);
        Gemm<T>{}(
                cublas_handle(), b_layout.trans, a_layout.trans, n, m, k, &one, b_ptr, b_layout.ld, a_ptr, a_layout.ld, &zero, out_ptr, n);
    };

    if (a.dtype() == Dtype::kFloat32) {
        gemm_impl(PrimitiveType<float>{});
    } else if (a.dtype() == Dtype::kFloat64) {
        gemm_impl(PrimitiveType<double>{});
    } else {
        throw NotImplementedError("dot is not implemented for non-float types in CUDA");
    }

    if (!is_out_contiguous) {
        Copy(out_contiguous, out);
    }
}

}  // namespace cuda
}  // namespace xchainer
