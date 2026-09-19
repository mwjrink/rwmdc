#pragma once

#include <lib/grim/logger.h>

// #include <dlfcn.h>
// #include <stdio.h>
// #include <stdlib.h>

// typedef void* (*MALLOCFN)(size_t);
// typedef void* (*CALLOCFN)(size_t, size_t);
// typedef void (*FREEFN)(void*);

// MALLOCFN __kill_malloc_real_malloc = (MALLOCFN)0;
// CALLOCFN __kill_malloc_real_calloc = (CALLOCFN)0;
// FREEFN   __kill_malloc_real_free   = (FREEFN)0;

// void* get_realfn(const char* fnm) {
//     void* pfunc = (void*)NULL;
//     DEBUG_LOG("get_realfn");
//     pfunc = dlsym(RTLD_NEXT, fnm);
//     if (pfunc)
//         DEBUG_LOG("Found original %s\n", fnm);
//     else
//         DEBUG_LOG("Did not find original %s\n", fnm);
//     return pfunc;
// }
//
// // TODO main could also just pass them in before including this file
// //   ~~ and we store them in a context instead of a global var
// //      We can't do this because we can't pass the context in to malloc or calloc :(
// void init_kill_malloc() {
//     __kill_malloc_real_malloc = (MALLOCFN)get_realfn("malloc");
//     if (__kill_malloc_real_malloc == NULL)
//         CRITICAL_LOG("Failed to find real malloc function.");
//
//     __kill_malloc_real_calloc = (CALLOCFN)get_realfn("calloc");
//     if (__kill_malloc_real_calloc == NULL)
//         CRITICAL_LOG("Failed to find real calloc function.");
//
//     __kill_malloc_real_free = (FREEFN)get_realfn("free");
//     if (__kill_malloc_real_free == NULL)
//         CRITICAL_LOG("Failed to find real free function.");
// }
//
// void* malloc(size_t s) {
//     DEBUG_LOG("malloc");
//     return __kill_malloc_real_malloc(s);
// }
//
// void* calloc(size_t s1, size_t s2) {
//     DEBUG_LOG("calloc");
//     return __kill_malloc_real_calloc(s1, s2);
// }
//
// void free(void* ptr) {
//     DEBUG_LOG("free");
//     __kill_malloc_real_free(ptr);
// }

// #define _GNU_SOURCE
// #include "tlsf.h"
// #include <dlfcn.h>
// #include <stdbool.h>
// #include <stdio.h>
// #include <stdlib.h>
// #include <sys/mman.h>
//
// // Function pointer types
// typedef void* (*malloc_ptr)(size_t);
// typedef void* (*calloc_ptr)(size_t, size_t);
// typedef void (*free_ptr)(void*);
//
// static malloc_ptr __real_malloc = NULL;
// static calloc_ptr __real_calloc = NULL;
// static free_ptr   __real_free   = NULL;
//
// tlsf_t _sandbox_pool = NULL;
// bool   _use_sandbox  = false;
//
// // Strict C Global Overrides
// void* malloc(size_t size) {
//     if (_use_sandbox && _sandbox_pool) {
//         return tlsf_malloc(_sandbox_pool, size);
//     }
//     // Lazy-load the real libc malloc using the dynamic loader
//     if (!__real_malloc)
//         __real_malloc = (malloc_ptr)dlsym(RTLD_NEXT, "malloc");
//     return __real_malloc(size);
// }
//
// void* calloc(size_t num, size_t size) {
//     if (_use_sandbox && _sandbox_pool) {
//         return tlsf_calloc(_sandbox_pool, num, size);
//     }
//     // Lazy-load the real libc calloc using the dynamic loader
//     if (!__real_calloc)
//         __real_calloc = (calloc_ptr)dlsym(RTLD_NEXT, "calloc");
//     return __real_calloc(num, size);
// }
//
// void free(void* ptr) {
//     if (_use_sandbox && _sandbox_pool) {
//         tlsf_free(_sandbox_pool, ptr);
//         return;
//     }
//     if (!__real_free)
//         __real_free = (free_ptr)dlsym(RTLD_NEXT, "free");
//     __real_free(ptr);
// }
