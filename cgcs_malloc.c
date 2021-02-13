#include "cgcs_malloc.h"

#include <stdarg.h>
#include <string.h>

#define CGCS_MALLOC_BLOCK_SIZE  4096

typedef char mem_t[CGCS_MALLOC_BLOCK_SIZE];
typedef struct header header_t;

static mem_t block;

static void mem_initialize();
//static void *mem_first_byte_address();
static void *mem_last_byte_address();
static header_t *mem_first_header_alignment();
static header_t *mem_last_possible_header_alignment();

struct header {
    int16_t m_size;
}; 

static header_t *header_next(header_t *self);
static int16_t header_alloc_size(header_t *self);

static bool header_is_free(header_t *self);
//static bool header_is_used(header_t *self);
static bool header_is_last(header_t *self);

static int16_t header_calculate_split_remainder_size(header_t *self, int16_t size_to_keep);

static void header_split_block(header_t *self, size_t size);
static void header_merge_with_next_block(header_t *self);
static void header_coalesce(header_t *self);

static void header_toggle_use_status(header_t *self);

static bool pointer_is_invalid(void *ptr);

static inline void mem_initialize() {
    ((header_t *)(block))->m_size = (CGCS_MALLOC_BLOCK_SIZE - sizeof(header_t));
}

/*
static inline void *mem_first_byte_address() {
    return ((void *)(block));
}
*/

static inline void *mem_last_byte_address() {
    return ((void *)(block + CGCS_MALLOC_BLOCK_SIZE - 1));
}

static inline header_t *mem_first_header_alignment() {
    return ((header_t *)(block));
}

static inline header_t *mem_last_possible_header_alignment() {
    return ((header_t *)(block + (CGCS_MALLOC_BLOCK_SIZE - sizeof(header_t))));
}

static inline header_t *header_next(header_t *self) {
    return ((header_t *)((char *)(self) + sizeof *self + header_alloc_size(self)));
}

static inline int16_t header_alloc_size(header_t *self) {
    return abs(self->m_size);
}

static inline bool header_is_free(header_t *self) {
    return self->m_size >= 0;
}

/*
static inline bool header_is_used(header_t *self) {
    return self->m_size < 0;
}
*/

static inline bool header_is_last(header_t *self) {
    return header_next(self) - 1 == mem_last_possible_header_alignment();
}

static inline int16_t header_calculate_split_remainder_size(header_t *self, int16_t size_to_keep) {
    return ((int16_t)(self->m_size - size_to_keep - sizeof *self));
}

static inline void header_merge_with_next_block(header_t *self) {
    header_t *next = header_next(self);
    self->m_size += next->m_size + sizeof *next;
}

static inline void header_toggle_use_status(header_t *self) {
    self->m_size = (~self->m_size) + 1;
}

static inline bool pointer_is_invalid(void *ptr) {
    header_t *h = (header_t *)(ptr) - 1;
    return (header_alloc_size(h) <= 0) 
        || (header_alloc_size(h) > (CGCS_MALLOC_BLOCK_SIZE - sizeof *h));
}

static void header_split_block(header_t *self, size_t size_to_keep) {
    header_t *new_header = (header_t *)((char *)(self) + (sizeof *self + size_to_keep));

    if (new_header >= mem_last_possible_header_alignment() || size_to_keep == 0 
    || size_to_keep >= (CGCS_MALLOC_BLOCK_SIZE - sizeof *new_header)) {
        return;
    }

    new_header->m_size = (self->m_size - size_to_keep) - sizeof *new_header;
    self->m_size = size_to_keep;
}

static void header_coalesce(header_t *self) {
    header_t *prev = NULL;

    while (self) {
        if (prev) {
            if (header_is_free(prev) && header_is_free(self)) {
                header_merge_with_next_block(prev);
            }
        }

        prev = self;
        self = header_is_last(self) ? NULL : header_next(self);
    }
}

void *cgcs_malloc_impl(size_t size, const char *filename, size_t lineno) {
    if (mem_first_header_alignment()->m_size == 0) {
        mem_initialize();
    }

    void *ptr = NULL;

    if (size == 0 || size > (CGCS_MALLOC_BLOCK_SIZE - sizeof(header_t))) {
        /*
            TODO error message
            Allocation input value must be within [1, CGCS_MALLOC_BLOCK_SIZE - sizeof(header_t) + 1) bytes.
            Attempted allocation: size
        */
    } else {
        header_t *curr = (header_t *)(block);
        header_t *next = NULL;

        while ((next = header_is_last(curr) ? NULL : header_next(curr))) {
            if (header_is_free(curr)) {
                if (next && header_is_free(next)) {
                    header_merge_with_next_block(curr);
                }

                if (header_alloc_size(curr) >= size) {
                    break;
                }
           }

           curr = next;
        }

        if (curr) {
            if (header_calculate_split_remainder_size(curr, size) >= 1) {
                header_split_block(curr, size);
            }

            header_toggle_use_status(curr);
            ptr = curr + 1;
        } else {
            /*
                TODO error message
                Unable to allocate size bytes. (header requires at least sizeof *curr bytes).
             */
        }
    }

    return ptr;
}

void cgcs_free_impl(void *ptr, const char *filename, size_t lineno) {
    if (pointer_is_invalid(ptr)) {
        return;
    }

    header_t *curr = (header_t *)(ptr);

    if (header_is_free(--curr)) {
        /*
            TODO error message
            Cannot release memory for inactive storage --
            did you already call free on this address?
         */
    } else {
        bool ptr_in_range = ((header_t *)(ptr) >= mem_first_header_alignment() + 1) 
        && (ptr <= mem_last_byte_address());

        if (ptr_in_range) {
            header_t *next = header_is_last(curr) ? NULL : header_next(curr);

            /*
            // Do we really need this,
            // if we are going to run header_coalesce((header_t *)(block)) anyway?
            if (next && header_is_free(next)) {
                header_merge_with_next_block(curr);
            }
            */

            header_toggle_use_status(curr);
            header_coalesce((header_t *)(block));
        } else {
            /*
                TODO error message
                This pointer does not refer to a valid allocation by cgcs_malloc_impl.
             */
        }
    }
}
