/*!
    \file       cgcs_malloc.h
    \brief      Header file for cgcs_malloc: malloc implementation

    \author     Gemuele Aludino
    \date       12 Feb 2021
 */

#ifndef CGCS_MALLOC_H
#define CGCS_MALLOC_H

#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>

// `cgcs_malloc/cgcs_free`: proxy functions designed for use by client
static void *cgcs_malloc(size_t size);
static void cgcs_free(void *ptr);

// `cgcs_malloc_impl`: memory allocator functions, allocate and free
void *cgcs_malloc_impl(size_t size, const char *filename, size_t lineno);
void cgcs_free_impl(void *ptr, const char *filename, size_t lineno);

/*!
    \brief      Proxy inline function;
                calls `cgcs_malloc_impl` with `__FILE__` and `__LINE__` macros

    \param[in]  size    Desired size for memory allocation

    \return     address of allocated memory from `cgcs_malloc_impl`;
                will be `NULL` if `cgcs_malloc_impl` failed.
 */
static inline void *cgcs_malloc(size_t size) {
    return cgcs_malloc_impl(size, __FILE__, __LINE__);
}

/*!
    \brief      Proxy inline function;
                calls `cgcs_free_impl` with `__FILE__` and `__LINE__` macros

    \param[in]  ptr     Pointer to memory resources that will be freed
 */
static inline void cgcs_free(void *ptr) {
    cgcs_free_impl(ptr, __FILE__, __LINE__);
}

/*!
    \def    USE_CGCS_MALLOC
    \brief  Directive to shorten `cgcs_malloc(size)` to `malloc(size)`
            and `cgcs_free(ptr)` to `free(ptr)`

    \details
    Example usage:

    ```c
    #define USE_CGCS_MALLOC
    #include "cgcs_malloc.h"
    ```

    Use `#define USE_CGCS_MALLOC` before `#include "cgcs_malloc.h"`
    to remove cgcs_ prefix from the public API.

    Note that you will not be able to call the stdlib
    malloc/free functions if `#define USE_CGCS_MALLOC`
    and `#include "cgcs_malloc.h"` are defined in the same
    translation unit.
 */
#ifdef USE_CGCS_MALLOC
/*!
    \def        malloc(size)
    \brief      
 */
#define malloc(size)    cgcs_malloc(size)
#define free(ptr)       cgcs_free(ptr)
#endif /* USE_CGCS_MALLOC */

#endif /* CGCS_MALLOC_H */
