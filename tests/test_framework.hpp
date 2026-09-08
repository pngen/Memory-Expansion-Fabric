#pragma once
// Minimal dependency-light test framework for Memory Expansion Fabric tests.
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <string>
#include <vector>

namespace mef_test {

struct Case { const char* name; void (*fn)(); };
inline std::vector<Case>& registry() { static std::vector<Case> r; return r; }
inline bool registerCase(const char* name, void (*fn)()) {
    registry().push_back(Case{name, fn}); return true;
}

inline int& failures() { static int f = 0; return f; }
inline int& checks() { static int c = 0; return c; }
inline const char*& currentTestName() { static const char* n = ""; return n; }

inline void report(bool ok, const char* file, int line, const std::string& msg) {
    ++checks();
    if (!ok) {
        ++failures();
        std::printf("FAIL [%s] %s:%d %s\n", currentTestName(), file, line, msg.c_str());
    }
}

inline std::string fmt(const char* f, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(buf, sizeof(buf), f, ap);
    va_end(ap);
    return std::string(buf);
}

} // namespace mef_test

#define TEST(name)                                                       \
    static void test_##name();                                           \
    static const bool reg_##name = mef_test::registerCase(#name, &test_##name); \
    static void test_##name()

#define CHECK(cond)                                                      \
    do {                                                                 \
        mef_test::report((cond), __FILE__, __LINE__, "CHECK(" #cond ")"); \
    } while (0)

#define CHECK_EQ(a, b)                                                   \
    do {                                                                 \
        auto _a = (a); auto _b = (b);                                    \
        mef_test::report((_a == _b), __FILE__, __LINE__,                 \
            mef_test::fmt("CHECK_EQ(%s, %s)", #a, #b));                  \
    } while (0)

#define CHECK_MSG(cond, ...)                                             \
    do {                                                                 \
        char _b[1024]; std::snprintf(_b, sizeof(_b), __VA_ARGS__);       \
        mef_test::report((cond), __FILE__, __LINE__,                     \
            std::string("CHECK(" #cond "): ") + _b);                     \
    } while (0)

inline int mefRunAllTests() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    int ran = 0;
    for (const auto& c : mef_test::registry()) {
        mef_test::currentTestName() = c.name;
        std::printf("=== TEST %s ===\n", c.name);
        c.fn();
        ++ran;
    }
    std::printf("\n%d checks, %d failures, %d tests\n",
                mef_test::checks(), mef_test::failures(), ran);
    return (mef_test::failures() == 0) ? 0 : 1;
}

#define MEF_MAIN()                                                       \
    int main() { return mefRunAllTests(); }
