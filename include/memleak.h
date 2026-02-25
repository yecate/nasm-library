/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright 1996-2025 The NASM Authors - All Rights Reserved */

/*
 * memleak.h - Memory leak detection for NASM
 *
 * This module provides memory leak detection by tracking all allocations
 * and deallocations. Enable by defining NASM_MEMLEAK_DEBUG.
 */

#ifndef NASM_MEMLEAK_H
#define NASM_MEMLEAK_H

#include "compiler.h"

#ifdef NASM_MEMLEAK_DEBUG

/* Memory leak detection functions */
void memleak_init(void);
void memleak_cleanup(void);
void *memleak_malloc(size_t size, const char *file, int line);
void *memleak_calloc(size_t nelem, size_t size, const char *file, int line);
void *memleak_realloc(void *ptr, size_t size, const char *file, int line);
void memleak_free(void *ptr, const char *file, int line);
char *memleak_strdup(const char *s, const char *file, int line);
char *memleak_strndup(const char *s, size_t len, const char *file, int line);

/* Print current allocation stats (for debugging cleanup) */
void memleak_print_current(const char *label);

/* Redirect allocation functions to tracking versions */
#define nasm_malloc(size)           memleak_malloc(size, __FILE__, __LINE__)
#define nasm_calloc(nelem, size)    memleak_calloc(nelem, size, __FILE__, __LINE__)
#define nasm_zalloc(size)           memleak_calloc(size, 1, __FILE__, __LINE__)
#define nasm_realloc(ptr, size)     memleak_realloc(ptr, size, __FILE__, __LINE__)
#define nasm_free(ptr)              memleak_free(ptr, __FILE__, __LINE__)
#define nasm_strdup(s)              memleak_strdup(s, __FILE__, __LINE__)
#define nasm_strndup(s, len)        memleak_strndup(s, len, __FILE__, __LINE__)

#endif /* NASM_MEMLEAK_DEBUG */

#endif /* NASM_MEMLEAK_H */
