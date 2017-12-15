#include "xchainer/array.h"

#include <array>
#include <cstddef>
#include <initializer_list>
#include <gtest/gtest.h>

namespace xchainer {
namespace {

class ArrayTest : public ::testing::Test {
 public:
  void SetUp() override {
    dtype_ = Dtype::kFloat32;
  }

  Array MakeArray(std::initializer_list<int64_t> shape) const {
     return {gsl::make_span(shape.begin(), shape.end()), dtype_};
  }

  Dtype dtype() const {
    return dtype_;
  }

 private:
  Dtype dtype_;
};

TEST_F(ArrayTest, Ctor) {
  Array x = MakeArray({2, 3, 4});
  EXPECT_EQ(dtype(), x.dtype());
  EXPECT_EQ(3, x.ndim());
  EXPECT_EQ(2 * 3 * 4, x.total_size());
  EXPECT_EQ(4, x.element_bytes());
  EXPECT_EQ(2 * 3 * 4 * 4, x.total_bytes());
  EXPECT_EQ(nullptr, x.data());
  EXPECT_EQ(nullptr, x.raw_data());
}

TEST_F(ArrayTest, SetContiguousData) {
  Array x = MakeArray({2, 3, 4});
  std::shared_ptr<void> data = std::unique_ptr<float[]>(new float[2 * 3 * 4]);
  x.SetContiguousData(data);

  EXPECT_EQ(data, x.data());
  EXPECT_EQ(data.get(), x.raw_data());
  EXPECT_TRUE(x.is_contiguous());
  EXPECT_EQ(0, x.offset());
}

}  // namespace
}  // namespace xchainer
