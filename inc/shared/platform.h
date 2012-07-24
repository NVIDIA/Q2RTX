#ifdef _WIN32
#define PRIz    "Iu"
#else
#define PRIz    "zu"
#endif

#ifdef _WIN32
#define LIBSUFFIX   ".dll"
#else
#define LIBSUFFIX   ".so"
#endif

#ifdef _WIN32
#define PATH_SEP_CHAR       '\\'
#define PATH_SEP_STRING     "\\"
#else
#define PATH_SEP_CHAR       '/'
#define PATH_SEP_STRING     "/"
#endif

#ifdef __GNUC__

#define q_printf(f, a)      __attribute__((format(printf, f, a)))
#define q_noreturn          __attribute__((noreturn))
#define q_malloc            __attribute__((malloc))
#if __GNUC__ >= 4
#define q_sentinel          __attribute__((sentinel))
#else
#define q_sentinel
#endif

#define q_likely(x)         __builtin_expect(!!(x), 1)
#define q_unlikely(x)       __builtin_expect(!!(x), 0)
#if __GNUC__ >= 4
#define q_offsetof(t, m)    __builtin_offsetof(t, m)
#else
#define q_offsetof(t, m)    ((size_t)&((t *)0)->m)
#endif

#if USE_GAME_ABI_HACK
#define q_gameabi           __attribute__((callee_pop_aggregate_return(0)))
#else
#define q_gameabi
#endif

#ifdef _WIN32
#define q_exported          __attribute__((dllexport))
#else
#define q_exported          __attribute__((visibility("default")))
#endif

#define q_unused            __attribute__((unused))

#else /* __GNUC__ */

#define q_printf(f, a)
#define q_noreturn
#define q_malloc
#define q_sentinel
#define q_packed

#define q_likely(x)         !!(x)
#define q_unlikely(x)       !!(x)
#define q_offsetof(t, m)    ((size_t)&((t *)0)->m)

#define q_gameabi

#ifdef _WIN32
#define q_exported          __declspec(dllexport)
#else
#define q_exported
#endif

#define q_unused

#endif /* !__GNUC__ */
