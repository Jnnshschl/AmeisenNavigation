#include "TestFramework.hpp"

#include <cmath>
#include <vector>

#include "Smoothing/BezierCurve.hpp"
#include "Smoothing/CatmullRomSpline.hpp"
#include "Smoothing/ChaikinCurve.hpp"

namespace {
std::vector<Vector3> ZigZag(int count)
{
    std::vector<Vector3> points;

    for (int i = 0; i < count; ++i)
    {
        points.emplace_back(static_cast<float>(i) * 10.0f, (i % 2) ? 5.0f : -5.0f, static_cast<float>(i));
    }

    return points;
}

bool AllFinite(const Path& path)
{
    for (const auto& p : path)
    {
        if (!p.IsFinite())
        {
            return false;
        }
    }

    return true;
}

/// Run a smoother into a path whose buffer is followed by guard values, verify nothing was written past it.
template <typename Fn>
void CheckCapacityRespected(int inputCount, int capacity, Fn&& smooth)
{
    const auto input = ZigZag(inputCount);
    Path output(capacity);

    smooth(input, output);

    CHECK(output.pointCount <= capacity);
    CHECK(output.pointCount >= 1);
    CHECK(AllFinite(output));
    // The destination is always kept, even when the buffer is too small for all samples.
    CHECK(output[output.pointCount - 1] == input.back());
}
} // namespace

TEST_CASE(Chaikin_KeepsEndpointsAndDoublesSegments)
{
    const auto input = ZigZag(5);
    Path output(64);

    ChaikinCurve::SmoothPath(input.data(), static_cast<int>(input.size()), output);

    CHECK_EQ(output.pointCount, 2 + 2 * (5 - 1));
    CHECK(output[0] == input.front());
    CHECK(output[output.pointCount - 1] == input.back());
    CHECK_NEAR(output[1].x, 2.5f, 1e-4);
    CHECK_NEAR(output[2].x, 7.5f, 1e-4);
}

TEST_CASE(Chaikin_NeverOverflows)
{
    for (int capacity = 1; capacity < 20; ++capacity)
    {
        CheckCapacityRespected(12, capacity, [](const std::vector<Vector3>& in, Path& out) {
            ChaikinCurve::SmoothPath(in.data(), static_cast<int>(in.size()), out);
        });
    }
}

TEST_CASE(CatmullRom_PassesThroughAllInputPoints)
{
    const auto input = ZigZag(6);
    Path output(256);

    CatmullRomSpline::SmoothPath(input.data(), static_cast<int>(input.size()), output, 4, 0.5f);

    CHECK(output[0] == input.front());
    CHECK(output[output.pointCount - 1] == input.back());
    CHECK(AllFinite(output));

    // Interpolating spline: every input point must appear in the output.
    for (const auto& p : input)
    {
        bool found = false;

        for (const auto& q : output)
        {
            found |= q == p;
        }

        CHECK(found);
    }

    // 5 segments * 4 samples + final point
    CHECK_EQ(output.pointCount, 5 * 4 + 1);
}

TEST_CASE(CatmullRom_DuplicatePointsDontProduceNaN)
{
    std::vector<Vector3> input{{0, 0, 0}, {0, 0, 0}, {10, 0, 0}, {10, 0, 0}, {20, 5, 0}};
    Path output(128);

    CatmullRomSpline::SmoothPath(input.data(), static_cast<int>(input.size()), output, 8, 0.5f);

    CHECK(AllFinite(output));
    CHECK(output[output.pointCount - 1] == input.back());
}

TEST_CASE(CatmullRom_NeverOverflows)
{
    for (int capacity = 1; capacity < 30; ++capacity)
    {
        CheckCapacityRespected(10, capacity, [](const std::vector<Vector3>& in, Path& out) {
            CatmullRomSpline::SmoothPath(in.data(), static_cast<int>(in.size()), out, 6, 0.5f);
        });
    }
}

TEST_CASE(Bezier_KeepsTailOfNonMultipleOfThreeInputs)
{
    // 6 points = one full cubic group (0..3) + 2 leftover points that used to be dropped.
    const auto input = ZigZag(6);
    Path output(128);

    BezierCurve::SmoothPath(input.data(), static_cast<int>(input.size()), output, 8);

    CHECK(output[0] == input.front());
    CHECK(output[output.pointCount - 1] == input.back());
    CHECK(AllFinite(output));

    // No duplicates at the group joints.
    for (int i = 1; i < output.pointCount; ++i)
    {
        CHECK(!(output[i] == output[i - 1]));
    }
}

TEST_CASE(Bezier_NeverOverflows)
{
    for (int capacity = 1; capacity < 30; ++capacity)
    {
        CheckCapacityRespected(11, capacity, [](const std::vector<Vector3>& in, Path& out) {
            BezierCurve::SmoothPath(in.data(), static_cast<int>(in.size()), out, 8);
        });
    }
}

TEST_CASE(Smoothing_TinyInputs)
{
    const auto input = ZigZag(2);
    Path output(16);

    ChaikinCurve::SmoothPath(input.data(), 2, output);
    CHECK_EQ(output.pointCount, 2);

    CatmullRomSpline::SmoothPath(input.data(), 2, output, 4, 0.5f);
    CHECK_EQ(output.pointCount, 2);

    BezierCurve::SmoothPath(input.data(), 2, output, 4);
    CHECK_EQ(output.pointCount, 2);

    BezierCurve::SmoothPath(input.data(), 0, output, 4);
    CHECK_EQ(output.pointCount, 0);
}
