#pragma once
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace test
{
    using Function = void (*)();
    inline std::vector<std::pair<std::string, Function>>& Registry()
    {
        static std::vector<std::pair<std::string, Function>> tests;
        return tests;
    }

    struct Registrar
    {
        Registrar(const char* name, Function function)
        {
            Registry().emplace_back(name, function);
        }
    };

    template <typename Actual, typename Expected>
    void ExpectEqual(const Actual& actual, const Expected& expected,
                     const char* actualText, const char* expectedText)
    {
        if (!(actual == expected))
        {
            throw std::runtime_error(std::string(actualText) + " != " + expectedText);
        }
    }

    inline void ExpectNear(double actual, double expected, double tolerance)
    {
        if (std::abs(actual - expected) > tolerance)
        {
            throw std::runtime_error("values differ outside tolerance");
        }
    }
}

#define TEST(name) \
    static void name(); \
    static test::Registrar name##_registration(#name, &name); \
    static void name()
#define EXPECT_EQ(actual, expected) test::ExpectEqual((actual), (expected), #actual, #expected)
#define EXPECT_NEAR(actual, expected, tolerance) test::ExpectNear((actual), (expected), (tolerance))
#define EXPECT_TRUE(value) do { if (!(value)) throw std::runtime_error(#value); } while (false)
