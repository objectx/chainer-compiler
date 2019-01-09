#include <algorithm>

#include <chainerx/axes.h>
#include <chainerx/context.h>
#include <chainerx/routines/creation.h>
#include <chainerx/routines/indexing.h>
#include <chainerx/routines/linalg.h>
#include <chainerx/routines/logic.h>
#include <chainerx/routines/manipulation.h>
#include <chainerx/routines/math.h>
#include <chainerx/routines/statistics.h>
#include <chainerx/shape.h>

#include <common/log.h>
#include <runtime/gen_xcvm_ops.h>
#include <runtime/xcvm_state.h>

namespace oniku {
namespace runtime {

namespace {

chainerx::Array ElementwiseMax(chainerx::Array a, chainerx::Array b) {
    // TODO(hamaji): Implement this in ChainerX.
    CHECK_EQ(a.dtype(), b.dtype());
    int64_t an = a.GetTotalSize();
    int64_t bn = b.GetTotalSize();
    chainerx::Array result;
    if (an == 1) {
        result = chainerx::Maximum(chainerx::AsScalar(a), b);
    } else if (bn == 1) {
        result = chainerx::Maximum(a, chainerx::AsScalar(b));
    } else {
        CHECK_EQ(an, bn) << "Max with broadcast not supported yet";
        WARN_ONCE("Slow element-wise Max");
        // Flatten views.
        chainerx::Array av = chainerx::Reshape(a, {an});
        chainerx::Array bv = chainerx::Reshape(b, {an});
        std::vector<chainerx::Array> maxes;
        for (int i = 0; i < an; ++i) {
            chainerx::Array m = chainerx::Maximum(chainerx::AsScalar(av.At({i})), bv.At({i}));
            maxes.push_back(chainerx::Reshape(m, {1}));
        }
        result = chainerx::Concatenate(maxes, 0);
        result = chainerx::Reshape(result, a.shape());
    }
    return result;
}

}  // namespace

chainerx::Array MaxOp::RunImpl(XCVMState* st, const std::vector<chainerx::Array>& inputs) {
    CHECK_LT(0, inputs.size());
    chainerx::Array result = inputs[0];
    for (size_t i = 1; i < inputs.size(); ++i) {
        result = ElementwiseMax(result, inputs[i]);
    }
    return result;
}

chainerx::Array ReduceMaxOp::RunImpl(XCVMState* st, const chainerx::Array& a) {
    return chainerx::AMax(a, GetXchainerAxes(axes), keepdims != 0);
}

chainerx::Array ReduceSumOp::RunImpl(XCVMState* st, const chainerx::Array& a) {
    return chainerx::Sum(a, GetXchainerAxes(axes), keepdims != 0);
}

chainerx::Array ReduceSumSquareOp::RunImpl(XCVMState* st, const chainerx::Array& a) {
    return chainerx::Sum(a * a, GetXchainerAxes(axes), keepdims != 0);
}

chainerx::Array ReduceSumToOp::RunImpl(XCVMState* st, const chainerx::Array& data, const chainerx::Array& shape) {
    const chainerx::Shape& from = data.shape();
    const chainerx::Shape& to = ArrayToShape(shape);
    CHECK_GE(from.size(), to.size()) << "Reduce requested but shape actually expands: " << from << " to=" << to;

    chainerx::Axes axes;
    for (int i = 0; i < from.size(); ++i) {
        int j = i - from.size() + to.size();
        if (j < 0) {
            axes.push_back(i);
        } else if (from[i] != to[j]) {
            CHECK_EQ(1, to[j]) << "ReduceSumTo shape mismatches: from=" << from << " to=" << to;
            axes.push_back(i);
        }
    }
    chainerx::Array result = chainerx::Sum(data, axes, true /* keepdims */);
    return chainerx::Reshape(result, to);
}

chainerx::Array ReduceMeanOp::RunImpl(XCVMState* st, const chainerx::Array& a) {
    return chainerx::Mean(a, GetXchainerAxes(axes), keepdims != 0);
}

std::tuple<chainerx::Array, chainerx::Array> DropoutOp::RunImpl(XCVMState* st, const chainerx::Array& data) {
    if (st->is_training()) {
        WARN_ONCE("Dropout for training is slow.");
        chainerx::Array rnd = SlowRandom(data.shape());
        chainerx::Array mask = CastTo(rnd > MakeScalarArray(ratio), data.dtype());
        chainerx::Array out = data * mask;
        return std::tuple<chainerx::Array, chainerx::Array>{out, mask};
    } else {
        chainerx::Array mask = chainerx::OnesLike(data);
        return std::tuple<chainerx::Array, chainerx::Array>{data, mask};
    }
}

}  // namespace runtime
}  // namespace oniku
