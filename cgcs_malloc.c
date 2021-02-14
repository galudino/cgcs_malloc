/*!
    \file       cgcs_malloc.c
    \brief      Source file for cgcs_malloc: malloc implementation

    \author     Gemuele Aludino
    \date       12 Feb 2021
 */

#include "cgcs_malloc.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

/*!
    \def        CGCS_MALLOC_BLOCK_SIZE
    \brief      Directive for size of allocator byte array

    \details
    Informs the size of the array 
    that will be used by the cgcs memory allocator functions.
 */
#define CGCS_MALLOC_BLOCK_SIZE  4096

/*!
    \typedef    mem_t
    \brief      Alias for `char[CGCS_MALLOC_BLOCK_SIZE]`

    \details
    All instances of `char[CGCS_MALLOC_BLOCK_SIZE]` will be addressed as `mem_t`.
    `mem_t` represents the memory source for the cgcs memory allocator functions.

    \see    CGCS_MALLOC_BLOCK_SIZE
 */
typedef char mem_t[CGCS_MALLOC_BLOCK_SIZE];

/*!
    \typedef    header_t
    \brief      Alias for `(struct header)`

    \details
    All instances of `(struct header)` will be addressed as `header_t`.
 */
typedef struct header header_t;

/*
    Global instance of byte array, in .BSS section of memory
 */
static mem_t block;

static void mem_initialize();
static void *mem_first_byte_address();
static void *mem_last_byte_address();
static header_t *mem_first_header_alignment();
static header_t *mem_last_possible_header_alignment();

/*!
    \typedef    header_t
    \brief      Alias for `(struct header)`

    \details
    All instances of `(struct header)` will be addressed as `header_t.`
 */
struct header {
    int16_t m_size; //! size of allocation after `header_t`, negative value means allocation is in use
}; 

static header_t *header_next(header_t *self);
static int16_t header_alloc_size(header_t *self);

static bool header_is_free(header_t *self);
static bool header_is_used(header_t *self);
static bool header_is_last(header_t *self);

static int16_t header_calculate_split_remainder_size(header_t *self, int16_t size_to_keep);

static void header_toggle_use_status(header_t *self);
//static bool header_is_corrupt(header_t *self);

static void header_split_block(header_t *self, size_t size);
static void header_merge_with_next_block(header_t *self);
static void header_coalesce(header_t *self);

static bool pointer_outside_block_range(void *ptr);

/*!
    \brief      Initializes block by assigning it its first header "node"
                with its starting value(s).

    \details
    A newly initialized block will have one header/node,
    with its allocated capacity:
        `(CGCS_MALLOC_BLOCK_SIZE - sizeof(header_t))`.

        The amount of usable bytes is the
            total size of `block` - size of the first header
 */
static inline void mem_initialize() {
    ((header_t *)(block))->m_size = (CGCS_MALLOC_BLOCK_SIZE - sizeof(header_t));
}

/*
    \brief    Return the base address of the allocation block 
              used by the memory allocator functions.

    \return   Address of `block[0]`, as `(void *)`
*/
static inline void *mem_first_byte_address() {
    return ((void *)(block));
}

/*!
    \brief      Return the address of the last byte
                within the allocation block used 
                by the memory allocation functions.

    \return     Address of `block[size - 1]`, as `(void *)` 
 */
static inline void *mem_last_byte_address() {
    return ((void *)(block + CGCS_MALLOC_BLOCK_SIZE - 1));
}

/*!
    \brief      Return the base address of the allocation block
                used by the memory allocator functions, as `(header_t *)`

    \return     Address of `block[0]`, as `(header_t *)`
 */
static inline header_t *mem_first_header_alignment() {
    return ((header_t *)(block));
}

/*!
    \brief      Return the address of the last `sizeof(header_t)`
                byte alignment within the allocation block used
                by the memory allocation functions.

    \details    The address of the last `sizeof(header_t)` byte chunk 
                within [block, block + size) is returned 
    
    \return     Address of `block[size - sizeof(header_t)]`, as `(header_t *)`
 */
static inline header_t *mem_last_possible_header_alignment() {
    return ((header_t *)(block + (CGCS_MALLOC_BLOCK_SIZE - sizeof(header_t))));
}

/*!
    \brief      Return the next header; the header to the "right" of `self`.      

    \details    Advance `(sizeof(header_t) + header_alloc_size(self))` bytes from `self`.

    \param[in]  self    The current `header_t`
    
    \return     The address that is `sizeof(header_t)` + `header_alloc_size(self)` away
                from `self`.
 */
static inline header_t *header_next(header_t *self) {
    return ((header_t *)((char *)(self) + sizeof *self + header_alloc_size(self)));
}

/*!
    \brief      Return the absolute value of `self->m_size`.

    \details    A negative value denotes that the block assigned to `self`
                is in use. A positive value denotes a free block.

    \param[in]  self    The current `header_t`
    
    \return     `abs(self->m_size)`
 */
static inline int16_t header_alloc_size(header_t *self) {
    return abs(self->m_size);
}

/*!
    \brief      Determine if the block addressed at `(self + 1)` is not in use.

    \param[in]  self    The current `header_t`
    
    \return     `true`, if `self->m_size >= 0`, `false` otherwise.
 */
static inline bool header_is_free(header_t *self) {
    return self->m_size >= 0;
}

/*!
    \brief      Determine if the block addressed at `(self + 1)` is in use.

    \param[in]  self    The current `header_t`
    
    \return     `true`, if `self->m_size < 0`, `false` otherwise.
 */
static inline bool header_is_used(header_t *self) {
    return self->m_size < 0;
}

/*!
    \brief      Determine if `self` is the last header within block.

    \details    Invoking `header_next(self)`, if `self` is the last header,
                should yield the address of the first byte outside of
                `block`. 
                
                Therefore, `header_next(self) - 1` should yield
                the address of the last `sizeof(header_t)` byte alignment
                within block.

                If `header_next(self) - 1` is the same address as that of
                `mem_last_possible_header_alignment()`, `self` is the address
                of the last `header_t` within block.

    \param[in]  self    The current `header_t`
    
    \return     `true`, if `self` is the last header in `block`, `false` otherwise.
 */
static inline bool header_is_last(header_t *self) {
    return header_next(self) - 1 == mem_last_possible_header_alignment();
}

/*!
    \brief      Determine the excess byte count if `self->m_size` was reduced
                to `size_to_keep`.

    \details    This function is used to determine if `self->m_size`
                can be reduced to `size_to_keep` 
                such that the portion split away from `self->m_size`
                (aka the remainder size) is greater than or equal to 1 byte.

    \param[in]  self            The current `header_t`
    \param[in]  size_to_keep    The desired size for `self->m_size`
    
    \return     `self->m_size - size_to_keep - sizeof(header_t)`
 */
static inline int16_t header_calculate_split_remainder_size(header_t *self, int16_t size_to_keep) {
    return ((int16_t)(self->m_size - size_to_keep - sizeof *self));
}

/*!
    \brief  Coalesces memory referred to by `self` and `self->next`
            into one block.
  
    \param[in]  self    The current `header_t`
  
    Precondition: `header_free(self) && header_free(header_next(self))`
 
    This function is called by `cgcs_malloc_impl` or `cgcs_free_impl`, when the precondition
    above is fulfilled. It is not to be called unless conditions for
    coalescence are clearly defined.
 */
static inline void header_merge_with_next_block(header_t *self) {
    header_t *next = header_next(self);
    self->m_size += next->m_size + sizeof *next;
}

/*!
    \brief      Mark the block assigned to self as in use, if it is free --
                otherwise, mark it as free, if it is in use.
    
    \details    Used for assigning/reclaiming a block.
                The two's complement of `self->m_size` is assigned to itself,
                which effectively amounts to a multiplication by (-1). 
    
    \param[in]  self    The current `header_t`
 */
static inline void header_toggle_use_status(header_t *self) {
    self->m_size = (~self->m_size) + 1;
}

/*!
    \brief      Can be used to check if header has an invalid `m_size` value.

    \details
    Under normal use, a `header_t` should never have an `m_size` value of 0,
    nor should it exceed a value of CGCS_MALLOC_BLOCK_SIZE - sizeof(header_t).

    This would only occur if an `header_t`'s `m_size` field was modified directly.

    \param[in]  self    The current `header_t`

    \return     `true`, if `self->m_size == 0 || self->m_size > CGCS_MALLOC_BLOCK_SIZE - sizeof(header_t)`
                false otherwise.
 */
/*
// UNUSED
static inline bool header_is_corrupt(header_t *self) {
    return self->m_size == 0 || self->m_size > CGCS_MALLOC_BLOCK_SIZE - sizeof *self;
}
*/

/*!
    \brief      Determines if `ptr`'s address is less than the first address at block,
                or if `ptr`'s address is greater than the last address at block.

    \param[in]  ptr     The pointer to assess
    
    \return     true,  if ptr < &block || ptr > &block + CGCS_MALLOC_BLOCK_SIZE - 1
                false, if ptr >= &block && ptr <= &block + CGCS_MALLOC_BLOCK_SIZE - 1
 */
static inline bool pointer_outside_block_range(void *ptr) {
    return ptr < mem_first_byte_address() || ptr > mem_last_byte_address();
}

/*!
    \brief  Creates a new block by partitioning the memory referred to
            by next into size bytes -- the remaining memory
            becomes part of the new block created in this function.
 
    Precondition: `header_is_free(self) && header_is_free(header_next(self))`,
                  else undefined behavior
 
    \param[in] self            The current `header_t`
    \param[in]  size_to_keep    The desired reduced size for `self->m_size`
 */
static void header_split_block(header_t *self, size_t size_to_keep) {
    /*
        We want to treat `self` as a `(char *)` --
        the address of a one-byte figure.
     
        That way, when we add any integral values to it,
        pointer arithmetic becomes standard arithmetic, and we
        can fine-tune the exact amount of bytes that we want to
        advance from address `next`.
     
        We want to find an address for `new_header`, and we will
        start from `next`.
     
        We will advance `sizeof(header_t)`, or `(sizeof *self)` bytes
        from `self`, plus the intended size for the block to be split.
     
        `(char *)(self) + (sizeof(header_t) + size)`
     
        But, we cannot simply assign this to `new_header`, because
        it has been type-coerced to be a `(char *)`.
        So, we cast the entirety of the expression to `(header_t *)`,
        the intended type.
     
        `(header_t *)((char *)(self) + (sizeof(header_t) + size))`
     */
    header_t *new_header = (header_t *)((char *)(self) + (sizeof *self + size_to_keep));

    /*
        Address range/input check
     */
    if (new_header >= mem_last_possible_header_alignment() || size_to_keep == 0 
    || size_to_keep >= (CGCS_MALLOC_BLOCK_SIZE - sizeof *new_header)) {
        return;
    }

    /*
        Now that `new_header` has been given its new home,
        its values can be assigned.
     
        The size of `new_header` will be
            `self->m_size` (`self`'s size, soon to be former size)
                minus
            the requested size, `size_to_keep` (what `self`'s size will become, shortly)
                minus
            `sizeof(header_t)`, or `sizeof *new_header`
      
        Remember, the header takes up room too, and that is why we must
        subtract `sizeof(header_t)` (or `sizeof *new_header`) from the overall
        quantity.
      
        The result, `new_header`, is a free (unoccupied) block.
     */
    new_header->m_size = (self->m_size - size_to_keep) - sizeof *new_header;
    self->m_size = size_to_keep;    // self will now take on its new size value.
}

/*!
    \brief  Traverses the `block` buffer by byte increments of
            `(sizeof *self + self->m_size)` and merges adjacent free blocks
            into contigious memory for future allocations.
  
    \param[in]  self    The current `header_t`
 
    Precondition: `self != NULL` and `mem_initialize` has been called
 */
static void header_coalesce(header_t *self) {
    header_t *prev = NULL;

    while (self) {
        /*
            If the header that was just visited is representing a free block,
            and the current header is also representing a free block,
            perform a coalescence between them.
         */
        if (prev) {
            if (header_is_free(prev) && header_is_free(self)) {
                header_merge_with_next_block(prev);
            }
        }

        /*
            If we haven't found what we are looking for yet,
            and self is not the last header, we move on to the next header.
         */
        prev = self;
        self = header_is_last(self) ? NULL : header_next(self);
    }
}

/*!
    \brief      Allocates size bytes from `block`
                and returns a pointer to the allocated memory.
  
    \param[in]  size        desired memory by user (in bytes)
    \param[in]  filename    for use with `__FILE__` directive
    \param[in]  lineno      for use with `__LINE__` directive
 
    \return     on success, a pointer to a block of memory of quantity size.
                on failure, `NULL`
 */
void *cgcs_malloc_impl(size_t size, const char *filename, size_t lineno) {
    /*
        If `cgcs_malloc_impl` has not been called yet,
        initialize the free list by creating a header
        within block, and giving the header its starting value(s).
     */
    if (mem_first_header_alignment()->m_size == 0) {
        mem_initialize();
    }

    void *ptr = NULL;

    /*
        First sanity check: 
        is the size request within 
            [1, `CGCS_MALLOC_BLOCK_SIZE` - `sizeof(header_t)` + 1) bytes?
        If not, do not continue -- return `NULL`.
     */
    if (size == 0 || size > (CGCS_MALLOC_BLOCK_SIZE - sizeof(header_t))) {
       fprintf(stderr, 
       "[ERROR: cgcs_malloc_impl] Allocation value must be within [1, %lu) bytes.\nAttempted allocation: %lu\n", 
       CGCS_MALLOC_BLOCK_SIZE - sizeof(header_t) + 1, size);
    } else {
        /*
            Cursor variable `curr` is set to the base address of `block`.
         */
        header_t *curr = (header_t *)(block);
        header_t *next = NULL;

        /*
            We traverse the free list (block) and search for
            a header that is associated with an unused block of memory.
     
            We reject headers denoting occupied blocks,
            and headers representing blocks of sizes less than what we are
            looking for.
     
            `curr` becomes `NULL` when there are no more blocks to traverse.

            On each iteration of the `while` loop,
            we retrieve the "lookahead" header from position `curr`.
        */
        while ((next = header_is_last(curr) ? NULL : header_next(curr))) {
            // If curr represents a free block...
            if (header_is_free(curr)) {
                /*
                    If `curr`'s right adjacent header (if applicable)
                    represents a free block, we can merge them together.
                    This should help to reduce fragmentation in the long run.
                 */
                if (next && header_is_free(next)) {
                    header_merge_with_next_block(curr);
                }

                /*
                    If the block represents by `curr` is greater than or equal
                    to the requested size, we can leave the loop.
                */
                if (header_alloc_size(curr) >= size) {
                    break;
                }
           }

           curr = next;
        }

        /*
            If `curr` is non-null, we have found what we are looking for.
         */
        if (curr) {
            /*
                If the block represented by `curr` is bigger than
                the requested value, size, it will be split,
                so that `curr` ends up representing a block with a count of
                size bytes.
            
                However, the split must also result in a second block
                with enough space to hold a new header representing a block
                of at least size 1.
            
                If the split were to occur such that there was not enough
                room for a header with a block of at least size 1,
                the block will not be split.
            
                `header_calculate_split_remainder_size(curr, size)` expands to:
                `curr->m_size - size - sizeof(header_t)`
            
                So,
                    the size of the block represented by `curr`
                        minus
                    the size requested for allocation by the client
                        minus
                    the size of `(header_t)` -- block metadata.
            
                must be greater than or equal to 1
                to be worth a split.
            
                Basically, `curr->m_size` (the size of a candidate block)
                must be at least (requested size + (`sizeof(header_t)` + 1))
                in order to qualify for a split.
            */
            if (header_calculate_split_remainder_size(curr, size) >= 1) {
                header_split_block(curr, size);
            }

            // `curr` is now an occupied block.
            header_toggle_use_status(curr);

            /*
                `ptr` is what will be returned from this function.
                `curr` + 1 is assigned to `ptr`, because we do not want to return
                the base address of the allocated block's header --
                we want to return the base address of the allocated block itself.

                Therefore, we "skip" over the header (metadata) by moving
                sizeof(header_t) bytes from address `curr`.

                Both
                    ptr = curr + 1;
                and
                    ptr = (char *)(curr) + sizeof(header_t);
                and
                    ptr = (char *)(curr) + sizeof *curr;
                are equivalent.

                `ptr` will now be the base address of the allocation requested by the caller.
            */
            ptr = curr + 1;
        } else {
            fprintf(stderr, 
            "[ERROR: cgcs_malloc_impl] Unable to allocate %lu bytes. (header requires at least %lu bytes.)\n", 
            size, sizeof *curr);
        }
    }

    return ptr;
}

/*!
    \brief      Frees the space pointer to by `ptr`, which must have been
                returns by a previous call to `cgcs_malloc_impl`.
  
    \param[out]  ptr         address of the memory to free
    \param[in]   filename    for use with the `__FILE__` directive
    \param[in]   lineno      for use with the `__LINE__` directive
 */
void cgcs_free_impl(void *ptr, const char *filename, size_t lineno) {
    /*
        Sanity check:
            Is `ptr` outside the range of [&ptr, ptr + CGCS_MALLOC_BLOCK_SIZE) ?
            If so, `return`.
     */
    if (pointer_outside_block_range(ptr)) {
        fprintf(stderr, "[ERROR: cgcs_free_impl] A free was attempted on a pointer that does not refer to a valid allocation by cgcs_malloc_impl.\n");
        return;
    }

    /*
        `ptr` is the address of the allocation that proceeds its header.
        To get to the header (and determine its occupancy status/`m_size` value),
        we type-coerce `ptr` as `(header_t *)`, and decrement the type-coerced address by 1.
     */
    header_t *curr = (header_t *)(ptr) - 1;
    
    if (header_is_used(curr)) {
        // `curr` will now represent an unoccupied block.
        // The proceeding block is now free for use.
        header_toggle_use_status(curr);

        // The entirety of `block` will also be searched for
        // adjacent free blocks to coalesce (combine).
        header_coalesce((header_t *)(block));
    } else {
        /*
            If `curr` reports that this block of memory
            is already free, there is nothing left to do but
            report the findings to the user -- return afterward.
         */
        fprintf(stderr, 
        "[ERROR: cgcs_free_impl] Cannot release memory for inactive storage -- did you already call free on this address?\n");        
    }
}

// Color macros
#define KNRM        "\x1B[0;0m" //!< reset to standard color/weight
#define KGRY        "\x1B[0;2m" //!< dark grey
#define KGRN        "\x1B[0;32m" //!< green
#define KCYN        "\x1B[0;36m" //!< cyan
#define KRED_b      "\x1B[1;31m" //!< red bold
#define KWHT_b      "\x1B[1;37m" //!< white bold

#define HEADER_FPUTS_NO_ALLOCS_MADE \
"------------------------------------------\n"\
"No allocations have been made yet.\n\n"\
"[%s:%lu] %s%s%s\n%s%s %s%s\n"\
"------------------------------------------\n\n"

#define HEADER_FPUTS_COLUMNS \
"------------------------------------------\n"\
"%sBlock Address%s\t%sStatus%s\t\t%sBlock Size%s\n"\
"-------------\t------\t\t----------\n"\

#define HEADER_FPUTS_STATS \
"------------------------------------------\n"\
"Used blocks in list:\t%s%u%s blocks\n"\
"Free blocks in list:\t%s%u%s blocks\n\n"\
"Free space:\t\t%s%u%s of %s%u%s bytes\n"\
"Available for client:\t%s%u%s of %s%u%s bytes\n\n"\
"Total data in use:\t%s%u%s of %s%u%s bytes\n"\
"Client data in use:\t%s%u%s of %s%u%s bytes\n\n"\
"Largest used block:\t%s%u%s of %s%u%s bytes\n"\
"Largest free block:\t%s%u%s of %s%u%s bytes\n\n"\
"Size of metadata:\t%s%lu%s bytes\n\n"\
"[%s:%lu] %s%s%s\n%s%s %s%s\n"\
"------------------------------------------\n\n"\

/*!
    \brief  Output the current state of block to a FILE stream dest
 
    \param[in]  dest        a FILE * stream, stdout, stderr, or a file
    \param[in]  filename    for use with the __FILE__ macro
    \param[in]  funcname    for use with the __func__ macro
    \param[in]  lineno      for use with the __LINE__ macro
 */
void header_fputs(FILE *dest, const char *filename, const char *funcname, size_t lineno) {
    struct {
        uint16_t block_used;
        uint16_t block_free;

        uint16_t space_used;
        uint16_t space_free;

        uint16_t bytes_in_use;
        uint16_t block_count_available;

        uint16_t largest_block_used;
        uint16_t largest_block_free;
    } info = { 0, 0, 0, 0, 0, 0, 0, 0 };

    header_t *h = (header_t *)(block);

    if (h->m_size == 0) {
        fprintf(dest, HEADER_FPUTS_NO_ALLOCS_MADE, 
        filename, lineno, KCYN, funcname, KNRM, KGRY, __DATE__, __TIME__, KNRM);
        return;
    }

    fprintf(dest, HEADER_FPUTS_COLUMNS, KWHT_b, KNRM, KWHT_b, KNRM, KWHT_b, KNRM);

    while (h) {
        bool header_free = header_is_free(h);

        info.block_used += header_free ? 0 : 1;
        info.block_free += header_free ? 1 : 0;

        info.space_used += header_free ? 0 : header_alloc_size(h);
        info.space_free += header_free ? header_alloc_size(h) : 0;

        info.largest_block_used =
            (info.largest_block_used < header_alloc_size(h)) && !header_free ?
                header_alloc_size(h) :
                info.largest_block_used;

        info.largest_block_free = (info.largest_block_free < header_alloc_size(h) && header_free ?
                                       header_alloc_size(h) :
                                       info.largest_block_free);

        fprintf(dest, "%s%p%s\t%s\t\t%d\n", 
        KGRY, (void *)(h + 1), KNRM, header_free ? KGRN"free"KNRM : KRED_b"in use"KNRM, header_alloc_size(h));

        h = header_is_last(h) ? NULL : header_next(h);
    }

    info.bytes_in_use =
        info.space_used + (sizeof *h * (info.block_used + info.block_free));

    info.block_count_available =
        CGCS_MALLOC_BLOCK_SIZE - (sizeof *h * (info.block_free + info.block_used));

    fprintf(dest, HEADER_FPUTS_STATS, 
        KWHT_b, info.block_used, KNRM,
        KWHT_b, info.block_free, KNRM,
        KWHT_b, info.space_free, KNRM, KWHT_b, CGCS_MALLOC_BLOCK_SIZE, KNRM,
        KWHT_b, info.space_free, KNRM, KWHT_b, info.block_count_available, KNRM,
        KWHT_b, info.bytes_in_use, KNRM, KWHT_b, CGCS_MALLOC_BLOCK_SIZE, KNRM,
        KWHT_b, info.space_used, KNRM, KWHT_b, info.block_count_available, KNRM,
        KWHT_b, info.largest_block_used, KNRM, KWHT_b, info.block_count_available, KNRM,
        KWHT_b, info.largest_block_free, KNRM, KWHT_b, info.block_count_available, KNRM,
        KWHT_b, sizeof(header_t), KNRM, 
        filename, lineno, KCYN, funcname, KNRM, KGRY, __DATE__, __TIME__, KNRM);
}
