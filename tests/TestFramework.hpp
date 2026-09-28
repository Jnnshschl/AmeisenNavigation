#pragma once

#include <cmath>
#include <cstdio>
#include <exception>
#include <functional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

/// Tiny self-contained test framework (no external dependencies, works offline and on every platform).
///
///   TEST_CASE(Name) { CHECK(a == b); REQUIRE(ptr != nullptr); CHECK_NEAR(x, y, 0.01); }
///
/// CHECK records a failure and continues, REQUIRE aborts the current test case.
namespace Test {
struct Case
{
    const char* name;
    void (*fn)();
};

inline std::vector<Case>& Registry()
{
    static std::vector<Case> cases;
    return cases;
}

struct Registrar
{
    Registrar(const char* name, void (*fn)()) { Registry().push_back({name, fn}); }
};

struct AbortTest
{
};

inline int& FailureCount()
{
    static int failures = 0;
    return failures;
}

inline int& CheckCount()
{
    static int checks = 0;
    return checks;
}

inline void ReportFailure(const char* file, int line, const std::string& message)
{
    FailureCount()++;
    std::fprintf(stderr, "  %s:%d: FAILED: %s\n", file, line, message.c_str());
}

template <typename T>
std::string ToString(const T& value)
{
    std::ostringstream ss;

    if constexpr (requires(std::ostream& os, const T& v) { os << v; })
    {
        ss << value;
    }
    else
    {
        ss << "<value>";
    }

    return ss.str();
}

/// Run all tests whose name contains `filter` (all if empty). Returns the process exit code.
inline int RunAll(std::string_view filter)
{
    int failedCases = 0;
    int ran = 0;

    for (const auto& testCase : Registry())
    {
        if (!filter.empty() && std::string_view(testCase.name).find(filter) == std::string_view::npos)
        {
            continue;
        }

        ran++;
        const int failuresBefore = FailureCount();
        std::fprintf(stderr, "[ RUN  ] %s\n", testCase.name);

        try
        {
            testCase.fn();
        }
        catch (const AbortTest&)
        {
        }
        catch (const std::exception& e)
        {
            ReportFailure(__FILE__, __LINE__, std::string("unexpected exception: ") + e.what());
        }
        catch (...)
        {
            ReportFailure(__FILE__, __LINE__, "unexpected unknown exception");
        }

        const bool passed = FailureCount() == failuresBefore;
        failedCases += passed ? 0 : 1;
        std::fprintf(stderr, "[ %s ] %s\n", passed ? " OK " : "FAIL", testCase.name);
    }

    std::fprintf(stderr, "\n%d test case(s), %d check(s), %d failed case(s)\n", ran, CheckCount(), failedCases);
    return failedCases == 0 && ran > 0 ? 0 : 1;
}
} // namespace Test

#define ANAV_TEST_CONCAT2(a, b) a##b
#define ANAV_TEST_CONCAT(a, b) ANAV_TEST_CONCAT2(a, b)

#define TEST_CASE(name)                                                                                                \
    static void name();                                                                                                \
    static ::Test::Registrar ANAV_TEST_CONCAT(registrar_, name)(#name, &name);                                         \
    static void name()

#define CHECK(cond)                                                                                                    \
    do                                                                                                                 \
    {                                                                                                                  \
        ::Test::CheckCount()++;                                                                                        \
        if (!(cond))                                                                                                   \
            ::Test::ReportFailure(__FILE__, __LINE__, #cond);                                                          \
    } while (0)

#define REQUIRE(cond)                                                                                                  \
    do                                                                                                                 \
    {                                                                                                                  \
        ::Test::CheckCount()++;                                                                                        \
        if (!(cond))                                                                                                   \
        {                                                                                                              \
            ::Test::ReportFailure(__FILE__, __LINE__, #cond);                                                          \
            throw ::Test::AbortTest{};                                                                                 \
        }                                                                                                              \
    } while (0)

#define CHECK_EQ(a, b)                                                                                                 \
    do                                                                                                                 \
    {                                                                                                                  \
        ::Test::CheckCount()++;                                                                                        \
        const auto& va_ = (a);                                                                                         \
        const auto& vb_ = (b);                                                                                         \
        if (!(va_ == vb_))                                                                                             \
            ::Test::ReportFailure(__FILE__, __LINE__,                                                                  \
                                  std::string(#a " == " #b " (") + ::Test::ToString(va_) + " vs "                      \
                                      + ::Test::ToString(vb_) + ")");                                                  \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                                                          \
    do                                                                                                                 \
    {                                                                                                                  \
        ::Test::CheckCount()++;                                                                                        \
        const double va_ = static_cast<double>(a);                                                                     \
        const double vb_ = static_cast<double>(b);                                                                     \
        if (!(std::fabs(va_ - vb_) <= static_cast<double>(eps)))                                                       \
            ::Test::ReportFailure(__FILE__, __LINE__,                                                                  \
                                  std::string(#a " ~= " #b " (") + std::to_string(va_) + " vs "                        \
                                      + std::to_string(vb_) + ")");                                                    \
    } while (0)
