/* Freestanding runtime declarations. The module links against bionic (libc/libdl/liblog) at load
 * time; build.py verifies every imported symbol is on an allowlist. */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdbool.h>

#define EXPORT __attribute__((visibility("default")))
#define UNUSED __attribute__((unused))
#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#define COUNTOF(a) (sizeof(a) / sizeof((a)[0]))

/* libdl / libc */
void *dlopen(const char *path, int flags);
void *dlsym(void *handle, const char *symbol);
int mprotect(void *address, size_t length, int protection);
int getpagesize(void);
void *memcpy(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
char *strstr(const char *haystack, const char *needle);
int snprintf(char *buffer, size_t size, const char *format, ...);
int vsnprintf(char *buffer, size_t size, const char *format, va_list args);
int __android_log_print(int priority, const char *tag, const char *format, ...);

struct timespec_rt { long tv_sec; long tv_nsec; };
int clock_gettime(int clock, struct timespec_rt *ts);

struct dl_phdr_info_rt {
    uintptr_t dlpi_addr;
    const char *dlpi_name;
    const void *dlpi_phdr;
    uint16_t dlpi_phnum;
};
int dl_iterate_phdr(int (*callback)(struct dl_phdr_info_rt *, size_t, void *), void *data);

#define RTLD_NOW 2
#define RTLD_NOLOAD 4
#define PROT_READ 1
#define PROT_WRITE 2
#define CLOCK_MONOTONIC 1

/* Logging (tag shared with the Java side). */
#define LOG_TAG "2K20Assist"
#define LOGI(...) __android_log_print(4, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(5, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(6, LOG_TAG, __VA_ARGS__)
/* Log only the first `n` occurrences, then every 64th. */
#define LOG_LIMITED(counter, n, ...) do { \
        unsigned long log_n_ = __atomic_add_fetch(&(counter), 1, __ATOMIC_RELAXED); \
        if (log_n_ <= (n) || (log_n_ & 63) == 0) LOGI(__VA_ARGS__); \
    } while (0)

static inline int load_int(const int *p) { return __atomic_load_n(p, __ATOMIC_RELAXED); }
static inline void store_int(int *p, int v) { __atomic_store_n(p, v, __ATOMIC_RELAXED); }

static inline double now_seconds(void) {
    struct timespec_rt ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static inline int finite_in(float v, float lo, float hi) { return v >= lo && v <= hi; }  /* false for NaN */
static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline float maxf(float a, float b) { return a > b ? a : b; }
static inline float minf(float a, float b) { return a < b ? a : b; }
/* Hardware square root (aarch64 FSQRT) - keeps the module free of libm imports. */
static inline float sqrt_rt(float v) {
    float r;
    __asm__("fsqrt %s0, %s1" : "=w"(r) : "w"(v));
    return r;
}
