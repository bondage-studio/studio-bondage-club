#pragma once

#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace sbctest {

struct TestCase {
    std::string name;
    std::function<void()> fn;
};

std::vector<TestCase>& registry();
extern int g_failures;
void check(bool cond, const char* expr, const char* file, int line);

struct Registrar {
    Registrar(std::string name, std::function<void()> fn) {
        registry().push_back({std::move(name), std::move(fn)});
    }
};

}  // namespace sbctest

#define SBC_TEST(name)                                                 \
    static void name();                                                \
    static ::sbctest::Registrar sbc_reg_##name(#name, name);           \
    static void name()

#define CHECK(cond) ::sbctest::check((cond), #cond, __FILE__, __LINE__)

#define CHECK_THROWS(stmt)                                                          \
    do {                                                                           \
        bool sbc_threw = false;                                                    \
        try {                                                                      \
            stmt;                                                                  \
        } catch (...) {                                                            \
            sbc_threw = true;                                                      \
        }                                                                          \
        ::sbctest::check(sbc_threw, "expected exception: " #stmt, __FILE__, __LINE__); \
    } while (0)
