// Minimal dependency-free test harness. One copy per module so tests run in a clean consumer project.
#pragma once
#include <cmath>
#include <cstdio>
#include <vector>

namespace chop_test {
struct Case {
  const char* name;
  void (*fn)();
};
inline std::vector<Case>& registry() {
  static std::vector<Case> r;
  return r;
}
inline int& failures() {
  static int f = 0;
  return f;
}
struct Reg {
  Reg(const char* n, void (*f)()) { registry().push_back({n, f}); }
};
inline int runAll() {
  int failedCases = 0;
  for (const Case& c : registry()) {
    const int before = failures();
    c.fn();
    if (failures() != before) {
      ++failedCases;
      std::fprintf(stderr, "[FAIL] %s\n", c.name);
    }
  }
  std::printf("%zu cases, %d failed\n", registry().size(), failedCases);
  return failedCases == 0 ? 0 : 1;
}
}  // namespace chop_test

#define CHOP_TEST(name)                                         \
  static void name();                                           \
  static chop_test::Reg chop_reg_##name(#name, name);           \
  static void name()

#define CHECK(cond)                                                              \
  do {                                                                           \
    if (!(cond)) {                                                               \
      std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
      ++chop_test::failures();                                                   \
    }                                                                            \
  } while (0)

#define CHECK_EQ(a, b) CHECK((a) == (b))
#define CHECK_NEAR(a, b, eps) CHECK(std::fabs(static_cast<double>(a) - static_cast<double>(b)) <= (eps))

#ifdef CHOP_TEST_MAIN
int main() { return chop_test::runAll(); }
#endif
