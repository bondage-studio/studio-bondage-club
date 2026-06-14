#include "test_framework.hpp"

namespace sbctest {

std::vector<TestCase>& registry() {
    static std::vector<TestCase> r;
    return r;
}

int g_failures = 0;

void check(bool cond, const char* expr, const char* file, int line) {
    if (!cond) {
        ++g_failures;
        std::cerr << "  CHECK failed: " << expr << "  (" << file << ":" << line << ")\n";
    }
}

}  // namespace sbctest

int main() {
    using namespace sbctest;
    int failed_tests = 0;
    for (auto& t : registry()) {
        int before = g_failures;
        try {
            t.fn();
        } catch (const std::exception& e) {
            ++g_failures;
            std::cerr << "  EXCEPTION in " << t.name << ": " << e.what() << "\n";
        }
        bool ok = g_failures == before;
        std::cout << (ok ? "[PASS] " : "[FAIL] ") << t.name << "\n";
        if (!ok) ++failed_tests;
    }
    std::cout << (registry().size() - failed_tests) << "/" << registry().size()
              << " tests passed\n";
    return failed_tests == 0 ? 0 : 1;
}
