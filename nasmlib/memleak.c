/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright 1996-2025 The NASM Authors - All Rights Reserved */

/*
 * memleak.c - Memory leak detection implementation for NASM
 */

#include "compiler.h"

#ifdef NASM_MEMLEAK_DEBUG

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "nasmlib.h"
#include "hashtbl.h"
#include "error.h"
#include "alloc.h"

/* Memory allocation record - using a simple linked list instead of hash table */
struct mem_record {
    void *ptr;              /* Allocated pointer */
    size_t size;            /* Allocation size */
    const char *file;       /* Source file */
    int line;               /* Source line */
    struct mem_record *next; /* Next record in the list */
};

/* Global tracking data */
static struct mem_record *mem_list_head = NULL;
static size_t total_allocated = 0;
static size_t total_freed = 0;
static size_t current_allocated = 0;
static size_t peak_allocated = 0;
static size_t alloc_count = 0;
static size_t free_count = 0;
static int memleak_initialized = 0;

#ifdef _MSC_VER
static __declspec(thread) int tracking_disabled = 0;
#else
static __thread int tracking_disabled = 0;
#endif

size_t _nasm_last_string_size;

fatal_func nasm_alloc_failed(void)
{
    nasm_critical("out of memory!");
}

/* Initialize memory leak detection */
void memleak_init(void)
{
    if (memleak_initialized)
        return;

    mem_list_head = NULL;
    memleak_initialized = 1;

    atexit(memleak_cleanup);

    fprintf(stderr, "[MEMLEAK] Memory leak detection enabled\n");
    fflush(stderr);
}

/* Track a memory allocation */
static void track_alloc(void *ptr, size_t size, const char *file, int line)
{
    struct mem_record *rec;

    if (!memleak_initialized)
        memleak_init();

    if (!ptr)
        return;

    /* Prevent recursive tracking */
    if (tracking_disabled)
        return;

    tracking_disabled = 1;

    /* Use raw malloc to avoid infinite recursion */
    rec = malloc(sizeof(struct mem_record));
    if (!rec) {
        fprintf(stderr, "[MEMLEAK] Failed to allocate tracking record!\n");
        tracking_disabled = 0;
        return;
    }

    rec->ptr = ptr;
    rec->size = size;
    rec->file = file;
    rec->line = line;

    /* Add to the head of the linked list */
    rec->next = mem_list_head;
    mem_list_head = rec;

    total_allocated += size;
    current_allocated += size;
    alloc_count++;

    if (current_allocated > peak_allocated)
        peak_allocated = current_allocated;

    tracking_disabled = 0;
}

/* Track a memory deallocation */
static void track_free(void *ptr, const char *file, int line)
{
    struct mem_record *rec;
    struct mem_record *prev;

    if (!memleak_initialized)
        memleak_init();

    if (!ptr)
        return;

    (void)file;
    (void)line;

    /* Prevent recursive tracking */
    if (tracking_disabled)
        return;

    tracking_disabled = 1;

    /* Search for the pointer in the linked list */
    prev = NULL;
    for (rec = mem_list_head; rec != NULL; rec = rec->next) {
        if (rec->ptr == ptr && rec->size > 0) {
            /* Found it - unlink and free the tracking record */
            total_freed += rec->size;
            current_allocated -= rec->size;
            free_count++;

            if (prev)
                prev->next = rec->next;
            else
                mem_list_head = rec->next;

            free(rec);
            break;
        }

        prev = rec;
    }

    tracking_disabled = 0;
}

/* Cleanup and report memory leaks */
void memleak_cleanup(void)
{
    struct mem_record *rec, *next;
    size_t leak_count = 0;
    size_t leak_bytes = 0;
    size_t report_limit = 200;
    const char *env_limit;

    if (!memleak_initialized)
        return;

    fprintf(stderr, "\n");
    fprintf(stderr, "========================================\n");
    fprintf(stderr, "Memory Leak Detection Report\n");
    fprintf(stderr, "========================================\n");
    fprintf(stderr, "Total allocated:     %zu bytes (%zu calls)\n",
            total_allocated, alloc_count);
    fprintf(stderr, "Total freed:         %zu bytes (%zu calls)\n",
            total_freed, free_count);
    fprintf(stderr, "Peak memory usage:   %zu bytes\n", peak_allocated);
    fprintf(stderr, "Current allocated:   %zu bytes\n", current_allocated);
    fprintf(stderr, "\n");

    env_limit = getenv("NASM_MEMLEAK_MAX_REPORT");
    if (env_limit && *env_limit) {
        char *endptr;
        const unsigned long long parsed = strtoull(env_limit, &endptr, 10);

        if (endptr != env_limit)
            report_limit = (size_t)parsed;
    }

    /* Report leaked memory - traverse the linked list */
    for (rec = mem_list_head; rec != NULL; rec = rec->next) {
        /* Only report memory that hasn't been freed (size > 0) */
        if (rec->size == 0)
            continue;

        if (leak_count == 0) {
            fprintf(stderr, "MEMORY LEAKS DETECTED:\n");
            fprintf(stderr, "----------------------------------------\n");
        }
        if (leak_count < report_limit) {
            fprintf(stderr, "[%zu] %zu bytes at %p\n",
                    leak_count + 1, rec->size, rec->ptr);
            fprintf(stderr, "     Allocated at %s:%d\n", rec->file, rec->line);
        }
        leak_count++;
        leak_bytes += rec->size;
    }

    if (leak_count > 0) {
        if (leak_count > report_limit) {
            fprintf(stderr, "... %zu more leaks omitted (set NASM_MEMLEAK_MAX_REPORT to change limit)\n",
                    leak_count - report_limit);
        }
        fprintf(stderr, "----------------------------------------\n");
        fprintf(stderr, "Total leaks: %zu allocations, %zu bytes\n",
                leak_count, leak_bytes);
    } else {
        fprintf(stderr, "No memory leaks detected!\n");
    }
    fprintf(stderr, "========================================\n");

    /* Free tracking records */
    rec = mem_list_head;
    while (rec != NULL) {
        next = rec->next;
        free(rec);
        rec = next;
    }
    mem_list_head = NULL;
    memleak_initialized = 0;
}

/* Print current allocation stats for debugging */
void memleak_print_current(const char *label)
{
    size_t leak_count = 0;
    size_t leak_bytes = 0;
    struct mem_record *rec;

    /* Simple grouping: track top allocation sites */
    struct {
        const char *file;
        int line;
        size_t count;
        size_t bytes;
    } top_sites[20];
    int num_sites = 0;
    int i;

    if (!memleak_initialized)
        return;

    for (rec = mem_list_head; rec != NULL; rec = rec->next) {
        if (rec->size > 0) {
            int found = 0;
            leak_count++;
            leak_bytes += rec->size;

            for (i = 0; i < num_sites; i++) {
                if (top_sites[i].file == rec->file && top_sites[i].line == rec->line) {
                    top_sites[i].count++;
                    top_sites[i].bytes += rec->size;
                    found = 1;
                    break;
                }
            }
            if (!found && num_sites < 20) {
                top_sites[num_sites].file = rec->file;
                top_sites[num_sites].line = rec->line;
                top_sites[num_sites].count = 1;
                top_sites[num_sites].bytes = rec->size;
                num_sites++;
            }
        }
    }

    fprintf(stderr, "[MEMLEAK] %s: %zu allocs, %zu bytes still alive\n",
            label, leak_count, leak_bytes);
    for (i = 0; i < num_sites; i++) {
        if (top_sites[i].count > 5) {
            fprintf(stderr, "  %s:%d => %zu allocs, %zu bytes\n",
                    top_sites[i].file, top_sites[i].line,
                    top_sites[i].count, top_sites[i].bytes);
        }
    }
}

/* Tracked allocation functions */
void *memleak_malloc(size_t size, const char *file, int line)
{
    void *ptr;

again:
    ptr = malloc(size);

    if (unlikely(!ptr)) {
        if (!size) {
            size = 1;
            goto again;
        }
        nasm_alloc_failed();
    }

    track_alloc(ptr, size, file, line);
    return ptr;
}

void *memleak_calloc(size_t nelem, size_t size, const char *file, int line)
{
    void *ptr;

again:
    ptr = calloc(nelem, size);

    if (unlikely(!ptr)) {
        if (!nelem || !size) {
            nelem = size = 1;
            goto again;
        }
        nasm_alloc_failed();
    }

    track_alloc(ptr, nelem * size, file, line);
    return ptr;
}

void *memleak_realloc(void *old_ptr, size_t size, const char *file, int line)
{
    void *new_ptr;
    struct mem_record *rec;

    if (!old_ptr)
        return memleak_malloc(size, file, line);

    if (unlikely(!size))
        size = 1;

    new_ptr = realloc(old_ptr, size);
    if (!new_ptr)
        nasm_alloc_failed();

    if (new_ptr != old_ptr) {
        track_free(old_ptr, file, line);
        track_alloc(new_ptr, size, file, line);
    } else {
        /* In-place realloc: update tracked size so statistics stay correct */
        if (!tracking_disabled) {
            tracking_disabled = 1;

            for (rec = mem_list_head; rec != NULL; rec = rec->next) {
                if (rec->ptr == old_ptr && rec->size > 0) {
                    if (size > rec->size) {
                        const size_t grow = size - rec->size;
                        total_allocated += grow;
                        current_allocated += grow;
                    } else if (size < rec->size) {
                        const size_t shrink = rec->size - size;
                        total_freed += shrink;
                        current_allocated -= shrink;
                    }

                    rec->size = size;
                    break;
                }
            }

            tracking_disabled = 0;
        }
    }

    return new_ptr;
}

void memleak_free(void *ptr, const char *file, int line)
{
    if (ptr) {
        track_free(ptr, file, line);
        free(ptr);
    }
}

char *memleak_strdup(const char *s, const char *file, int line)
{
    size_t len = strlen(s) + 1;
    char *ptr = malloc(len);

    if (!ptr)
        nasm_alloc_failed();

    memcpy(ptr, s, len);
    _nasm_last_string_size = len;
    track_alloc(ptr, len, file, line);
    return ptr;
}

char *memleak_strndup(const char *s, size_t maxlen, const char *file, int line)
{
    size_t len = strnlen(s, maxlen);
    char *ptr = malloc(len + 1);

    if (!ptr)
        nasm_alloc_failed();

    memcpy(ptr, s, len);
    ptr[len] = '\0';
    _nasm_last_string_size = len + 1;
    track_alloc(ptr, len + 1, file, line);
    return ptr;
}

char *nasm_strcat(const char *one, const char *two)
{
    char *rslt;
    const size_t l1 = strlen(one);
    const size_t s2 = strlen(two) + 1;

    _nasm_last_string_size = l1 + s2;
    rslt = malloc(l1 + s2);
    if (!rslt)
        nasm_alloc_failed();

    memcpy(rslt, one, l1);
    memcpy(rslt + l1, two, s2);
    track_alloc(rslt, l1 + s2, __FILE__, __LINE__);
    return rslt;
}

char *nasm_strcatn(const char *str1, ...)
{
    va_list ap;
    char *rslt;
    size_t s;
    size_t n;
    size_t *ltbl;
    size_t l, *lp;
    const char *p;
    char *q;

    n = 0;
    p = str1;
    va_start(ap, str1);
    while (p) {
        n++;
        p = va_arg(ap, const char *);
    }
    va_end(ap);

    ltbl = malloc(n * sizeof(size_t));
    if (!ltbl)
        nasm_alloc_failed();

    s = 1;
    p = str1;
    lp = ltbl;
    va_start(ap, str1);
    while (p) {
        *lp++ = l = strlen(p);
        s += l;
        p = va_arg(ap, const char *);
    }
    va_end(ap);

    _nasm_last_string_size = s;

    q = rslt = malloc(s);
    if (!rslt)
        nasm_alloc_failed();

    p = str1;
    lp = ltbl;
    va_start(ap, str1);
    while (p) {
        l = *lp++;
        memcpy(q, p, l);
        q += l;
        p = va_arg(ap, const char *);
    }
    va_end(ap);
    *q = '\0';

    free(ltbl);
    track_alloc(rslt, s, __FILE__, __LINE__);

    return rslt;
}

#endif /* NASM_MEMLEAK_DEBUG */
