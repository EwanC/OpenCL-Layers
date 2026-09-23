// Copyright (c) 2025-2026 Ewan Crawford
#ifndef __LOGGER_H
#define __LOGGER_H

#include <cstdio>

// Debug logging, enabled with the REC_DEBUG CMake option.
#ifdef REC_DEBUG
#define REC_LOG(...)                                                           \
  do {                                                                         \
    std::fprintf(stderr, "[CLRecordLayer] ");                                  \
    std::fprintf(stderr, __VA_ARGS__);                                         \
    std::fprintf(stderr, "\n");                                                \
  } while (0)
#else
#define REC_LOG(...)                                                           \
  do {                                                                         \
  } while (0)
#endif

#endif /* __LOGGER_H */
