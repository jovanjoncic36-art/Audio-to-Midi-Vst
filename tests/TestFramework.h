#pragma once
// Minimal self-contained test harness (no external dependencies, works offline).

#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

namespace tf
{
struct TestCase
{
    const char* name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry()
{
    static std::vector<TestCase> r;
    return r;
}

inline int& currentFailures()
{
    static int f = 0;
    return f;
}

struct Registrar
{
    Registrar (const char* n, std::function<void()> f) { registry().push_back ({ n, std::move (f) }); }
};
} // namespace tf

#define TEST(name)                                          \
    static void name();                                     \
    static tf::Registrar registrar_##name (#name, name);    \
    static void name()

#define CHECK(cond)                                                                 \
    do                                                                              \
    {                                                                               \
        if (! (cond))                                                               \
        {                                                                           \
            std::printf ("    FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            ++tf::currentFailures();                                                \
        }                                                                           \
    } while (false)

#define CHECK_NEAR(a, b, tol)                                                                          \
    do                                                                                                 \
    {                                                                                                  \
        const double va_ = (double) (a), vb_ = (double) (b);                                           \
        if (! (std::fabs (va_ - vb_) <= (double) (tol)))                                               \
        {                                                                                              \
            std::printf ("    FAILED %s:%d: %s (%g) not near %s (%g), tol %g\n",                       \
                         __FILE__, __LINE__, #a, va_, #b, vb_, (double) (tol));                        \
            ++tf::currentFailures();                                                                   \
        }                                                                                              \
    } while (false)
