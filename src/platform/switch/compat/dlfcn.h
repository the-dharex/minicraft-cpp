#ifndef MINICRAFT_PLATFORM_SWITCH_COMPAT_DLFCN_H_
#define MINICRAFT_PLATFORM_SWITCH_COMPAT_DLFCN_H_

#include <stddef.h>

#define RTLD_NOW 0
#define RTLD_GLOBAL 0

static inline void* dlopen(const char* filename, int flags) {
  (void)filename;
  (void)flags;
  return NULL;
}

static inline void* dlsym(void* handle, const char* symbol) {
  (void)handle;
  (void)symbol;
  return NULL;
}

static inline int dlclose(void* handle) {
  (void)handle;
  return 0;
}

#endif  // MINICRAFT_PLATFORM_SWITCH_COMPAT_DLFCN_H_
