#include <lib/grim/logger.h>

// HACK to kill assert defined in std c headers
#undef assert

#define STATIC_ASSERT(condition) typedef char p__LINE__[(condition) ? 1 : -1]

#if !defined(ENABLE_ASSERT)
#define ENABLE_ASSERT 1
#endif

#define stmnt(s)                                                                                                       \
    do {                                                                                                               \
        s                                                                                                              \
    } while (0)

#if !defined(assert_break)
#define assert_break() _exit(1)
// (*(volatile int*)0 = 0)
#endif

#if ENABLE_ASSERT
#define assert(scope, c)                                                                                               \
    stmnt(if (!(c)) {                                                                                                  \
        CRITICAL_LOG(scope, "Assert failed.");                                                                         \
        assert_break();                                                                                                \
    })
#else
#define assert(c)
#endif

// #define static_assert(c, l) typedef u8 glue(l, __LINE__)[(c) ? 1 : -1]
