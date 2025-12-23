/**
 * @file mm.c
 * @brief A 64-bit struct-based implicit free list memory allocator
 *
 * 15-213: Introduction to Computer Systems
 *
 * TODO: insert your documentation here. :)
 *
 *************************************************************************
 *
 * ADVICE FOR STUDENTS.
 * - Step 0: Please read the writeup!
 * - Step 1: Write your heap checker.
 * - Step 2: Write contracts / debugging assert statements.
 * - Good luck, and have fun!
 *
 *************************************************************************
 *
 * @author Yu-Chi Pai <ypai@andrew.cmu.edu>
 */

#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "memlib.h"
#include "mm.h"

/* Do not change the following! */

#ifdef DRIVER
/* create aliases for driver tests */
#define malloc mm_malloc
#define free mm_free
#define realloc mm_realloc
#define calloc mm_calloc
#define memset mem_memset
#define memcpy mem_memcpy
#endif /* def DRIVER */

/* You can change anything from here onward */

/*
 *****************************************************************************
 * If DEBUG is defined (such as when running mdriver-dbg), these macros      *
 * are enabled. You can use them to print debugging output and to check      *
 * contracts only in debug mode.                                             *
 *                                                                           *
 * Only debugging macros with names beginning "dbg_" are allowed.            *
 * You may not define any other macros having arguments.                     *
 *****************************************************************************
 */
#ifdef DEBUG
/* When DEBUG is defined, these form aliases to useful functions */
#define dbg_requires(expr) assert(expr)
#define dbg_assert(expr) assert(expr)
#define dbg_ensures(expr) assert(expr)
#define dbg_printf(...) ((void)printf(__VA_ARGS__))
#define dbg_printheap(...) print_heap(__VA_ARGS__)
#else
/* When DEBUG is not defined, these should emit no code whatsoever,
 * not even from evaluation of argument expressions.  However,
 * argument expressions should still be syntax-checked and should
 * count as uses of any variables involved.  This used to use a
 * straightforward hack involving sizeof(), but that can sometimes
 * provoke warnings about misuse of sizeof().  I _hope_ that this
 * newer, less straightforward hack will be more robust.
 * Hat tip to Stack Overflow poster chqrlie (see
 * https://stackoverflow.com/questions/72647780).
 */
#define dbg_discard_expr_(...) ((void)((0) && printf(__VA_ARGS__)))
#define dbg_requires(expr) dbg_discard_expr_("%d", !(expr))
#define dbg_assert(expr) dbg_discard_expr_("%d", !(expr))
#define dbg_ensures(expr) dbg_discard_expr_("%d", !(expr))
#define dbg_printf(...) dbg_discard_expr_(__VA_ARGS__)
#define dbg_printheap(...) ((void)((0) && print_heap(__VA_ARGS__)))
#endif

#define NUM_ROOTS 14

/* Basic constants */

typedef uint64_t word_t;

/** @brief Word and header size (bytes) */
static const size_t wsize = sizeof(word_t);

/** @brief Double word size (bytes) */
static const size_t dsize = 2 * wsize; // 16 bytes

/** @brief Minimum block size (bytes) */
static const size_t min_block_size = 2 * dsize; // 32 bytes

static const size_t new_mini_block_size = 2 * wsize; // 16 bytes

/**
 * Heap grows in chunks of this size
 * (Must be divisible by dsize)
 */
static const size_t chunksize = (1 << 12); // 4096 bytes

/**
 * alloc_mask = 0x00000001, for extracting "a" bit in header
 */
static const word_t alloc_mask = 0x1;

/**
 * alloc_mask = 0x00000010, for extracting "pa" bit in header
 */
static const word_t prev_alloc_mask = 0x2;

/**
 * alloc_mask = 0x00000100, for extracting "pm" bit in header
 */
static const word_t prev_mini_mask = 0x4;

/**
 * size_mask = 0xFFFFFFF0, for extracting size in header
 */
static const word_t size_mask = ~(word_t)0xF;

/** @brief Represents the header and payload of one block in the heap */
typedef struct block {
    /** @brief Header contains size + allocation flag */
    word_t header;

    /**
     * @brief A pointer to the block payload.
     *
     * TODO: feel free to delete this comment once you've read it carefully.
     * We don't know what the size of the payload will be, so we will declare
     * it as a zero-length array, which is a GNU compiler extension. This will
     * allow us to obtain a pointer to the start of the payload. (The similar
     * standard-C feature of "flexible array members" won't work here because
     * those are not allowed to be members of a union.)
     *
     * WARNING: A zero-length array must be the last element in a struct, so
     * there should not be any struct fields after it. For this lab, we will
     * allow you to include a zero-length array in a union, as long as the
     * union is the last field in its containing struct. However, this is
     * compiler-specific behavior and should be avoided in general.
     *
     * WARNING: DO NOT cast this pointer to/from other types! Instead, you
     * should use a union to alias this zero-length array with another struct,
     * in order to store additional types of data in the payload memory.
     */
    char payload[0]; // cahr payload[]
} block_t;

/* free pointers of explict list */
typedef struct free_pointers {
    struct block *next;
    struct block *prev;
} free_pointers_t;

/* Global variables */

/** @brief Pointer to first block in the heap (implicit list) */
static block_t *heap_start = NULL; // 8 bytes

/** @brief Pointers to first blocks in the segregated list */
static block_t *free_roots[NUM_ROOTS] = {NULL};

/** @brief Head of the singly-linked list for mini free blocks */
static block_t *mini_free_root = NULL;

/*
 *****************************************************************************
 * The functions below are short wrapper functions to perform                *
 * bit manipulation, pointer arithmetic, and other helper operations.        *
 *                                                                           *
 * We've given you the function header comments for the functions below      *
 * to help you understand how this baseline code works.                      *
 *                                                                           *
 * Note that these function header comments are short since the functions    *
 * they are describing are short as well; you will need to provide           *
 * adequate details for the functions that you write yourself!               *
 *****************************************************************************
 */

/*
 * ---------------------------------------------------------------------------
 *                        BEGIN SHORT HELPER FUNCTIONS
 * ---------------------------------------------------------------------------
 */

/**
 * @brief Returns the maximum of two integers.
 * @param[in] x
 * @param[in] y
 * @return `x` if `x > y`, and `y` otherwise.
 */
static size_t max(size_t x, size_t y) {
    return (x > y) ? x : y;
}

/**
 * @brief Rounds `size` up to next multiple of n
 * @param[in] size
 * @param[in] n
 * @return The size after rounding up
 */
static size_t round_up(size_t size, size_t n) {
    return n * ((size + (n - 1)) / n);
}

/**
 * @brief Packs the `size` and `alloc` of a block into a word suitable for
 *        use as a packed value.
 *
 * Packed values are used for both headers and footers.
 *
 * The allocation status is packed into the lowest bit of the word.
 *
 * @param[in] size The size of the block being represented
 * @param[in] alloc True if the block is allocated
 * @return The packed value
 */
static word_t pack(size_t size, bool prev_mini, bool prev_alloc, bool alloc) {
    word_t word = size;
    if (alloc) {
        word |= alloc_mask;
    }
    if (prev_alloc) {
        word |= prev_alloc_mask;
    }
    if (prev_mini) {
        word |= prev_mini_mask;
    }
    return word;
}

/**
 * @brief Extracts the size represented in a packed word.
 *
 * This function simply clears the lowest 4 bits of the word, as the heap
 * is 16-byte aligned.
 *
 * @param[in] word
 * @return The size of the block represented by the word
 */
static size_t extract_size(word_t word) {
    return (word & size_mask);
}

/**
 * @brief Extracts the size of a block from its header.
 * @param[in] block
 * @return The size of the block
 */
static size_t get_size(block_t *block) {
    return extract_size(block->header);
}

/**
 * @brief Given a payload pointer, returns a pointer to the corresponding
 *        block.
 * @param[in] bp A pointer to a block's payload
 * @return The corresponding block
 */
static block_t *payload_to_header(void *bp) {
    return (block_t *)((char *)bp - offsetof(block_t, payload));
}

/**
 * @brief Given a block pointer, returns a pointer to the corresponding
 *        payload.
 * @param[in] block
 * @return A pointer to the block's payload
 * @pre The block must be a valid block, not a boundary tag.
 */
static void *header_to_payload(block_t *block) {
    dbg_requires(get_size(block) != 0);
    return (void *)(block->payload);
}

/**
 * @brief Returns the allocation status of a given header value.
 *
 * This is based on the lowest bit of the header value.
 *
 * @param[in] word
 * @return The allocation status correpsonding to the word
 */
static bool extract_alloc(word_t word) {
    return (bool)(word & alloc_mask);
}

/**
 * @brief Returns the previous allocation status of a given header value.
 *
 * This is based on the second lowest bit of the header value.
 *
 * @param[in] word
 * @return The previous allocation status correpsonding to the word
 */
static bool extract_prev_alloc(word_t word) {
    return (bool)(word & prev_alloc_mask);
}

/**
 * @brief Returns if the previous block is a mini block of a given header value.
 *
 * This is based on the third lowest bit of the header value.
 *
 * @param[in] word
 * @return The previous block is a mini block or not correpsonding to the word
 */
static bool extract_prev_mini(word_t word) {
    return (bool)(word & prev_mini_mask);
}

/**
 * @brief Returns the allocation status of a block, based on its header.
 * @param[in] block
 * @return The allocation status of the block
 */
static bool get_alloc(block_t *block) {
    return extract_alloc(block->header);
}

/**
 * @brief Returns the previous allocation status of a block, based on its
 * header.
 * @param[in] block
 * @return The previous allocation status of the block
 */
static bool get_prev_alloc(block_t *block) {
    return extract_prev_alloc(block->header);
}

/**
 * @brief Returns if the previous block is a mini block of a block, based on its
 * header.
 * @param[in] block
 * @return The previous block is a mini block or not of the block
 */
static bool get_prev_mini(block_t *block) {
    return extract_prev_mini(block->header);
}

/**
 * @brief Given a block pointer, returns a pointer to the corresponding
 *        footer.
 * @param[in] block
 * @return A pointer to the block's footer
 * @pre The block must be a valid block, not a boundary tag.
 */
static word_t *header_to_footer(block_t *block) {
    dbg_requires(get_size(block) != 0 &&
                 "Called header_to_footer on the epilogue block");
    dbg_requires(!get_alloc(block));
    dbg_requires(get_size(block) != new_mini_block_size);
    return (word_t *)(block->payload + get_size(block) - dsize);
}

/**
 * @brief Given a block footer, returns a pointer to the corresponding
 *        header.
 * @param[in] footer A pointer to the block's footer
 * @return A pointer to the start of the block
 * @pre The footer must be the footer of a valid block, not a boundary tag.
 */
static block_t *footer_to_header(word_t *footer) {
    size_t size = extract_size(*footer);
    dbg_assert(size != 0 && "Called footer_to_header on the prologue block");
    return (block_t *)((char *)footer + wsize - size);
}

/**
 * @brief Returns the payload size of a given block.
 *
 * The payload size is equal to the entire block size minus the sizes of the
 * block's header and/x footer. (free/allocated)
 *
 * @param[in] block
 * @return The size of the block's payload
 */
static size_t get_payload_size(block_t *block) {
    size_t asize = get_size(block);
    if (get_alloc(block)) {
        return asize - wsize;
    } else {
        return asize - dsize;
    }
}

/**
 * @brief Finds the next consecutive block on the heap.
 *
 * This function accesses the next block in the "implicit list" of the heap
 * by adding the size of the block.
 *
 * @param[in] block A block in the heap
 * @return The next consecutive block on the heap
 * @pre The block is not the epilogue
 */
static block_t *find_next(block_t *block) {
    dbg_requires(block != NULL);
    dbg_requires(get_size(block) != 0 &&
                 "Called find_next on the last block in the heap");
    return (block_t *)((char *)block + get_size(block));
}

/**
 * @brief Writes an epilogue header at the given address.
 *
 * The epilogue header has size 0, and is marked as allocated.
 *
 * @param[out] block The location to write the epilogue header
 */
static void write_epilogue(block_t *block, bool prev_mini, bool prev_alloc) {
    dbg_requires(block != NULL);
    dbg_requires((char *)block == (char *)mem_heap_hi() - 7);
    block->header = pack(0, prev_mini, prev_alloc, true);
}

/**
 * @brief Writes a block starting at the given address.
 *
 * This function writes both a header and footer, where the location of the
 * footer is computed in relation to the header.
 *
 *
 * @param[out] block The location to begin writing the block header
 * @param[in] size The size of the new block
 * @param[in] alloc The allocation status of the new block
 */
static void write_block(block_t *block, size_t size, bool prev_mini,
                        bool prev_alloc, bool alloc) {
    dbg_requires(block != NULL);
    dbg_requires(size > 0);

    block->header = pack(size, prev_mini, prev_alloc, alloc);

    // add footer to a non-mini free block
    if (!alloc && size != new_mini_block_size) {
        word_t *footerp = header_to_footer(block);
        *footerp = pack(size, prev_mini, prev_alloc, alloc);
    }

    // update next block's pa bit & pm bit
    block_t *block_next = find_next(block);
    if (!alloc) {
        block_next->header &= (~prev_alloc_mask); // pa = 0
    } else {
        block_next->header |= prev_alloc_mask; // pa = 1
    }

    if (size == new_mini_block_size) {
        block_next->header |= prev_mini_mask; // pm = 1
    } else {
        block_next->header &= (~prev_mini_mask); // pm = 0
    }
}

static bool is_in_heap_boundary(void *pointer) {
    return pointer >= mem_heap_lo() && pointer <= mem_heap_hi();
}

/**
 * @brief Finds the footer of the previous block on the heap.
 * @param[in] block A block in the heap
 * @return The location of the previous block's footer
 */
static word_t *find_prev_footer(block_t *block) {
    // Compute previous footer position as one word before the header
    return &(block->header) - 1;
}

/**
 * @brief Finds the previous consecutive block on the heap.
 *
 * This is the previous block in the "implicit list" of the heap.
 *
 * If the function is called on the first block in the heap, NULL will be
 * returned, since the first block in the heap has no previous block!
 *
 * The position of the previous block is found by reading the previous
 * block's footer to determine its size, then calculating the start of the
 * previous block based on its size.
 *
 * @param[in] block A block in the heap
 * @return The previous consecutive block in the heap.
 */
static block_t *find_prev(block_t *block) {
    dbg_requires(block != NULL);

    if (get_prev_alloc(block)) {
        return NULL;
    }

    if (get_prev_mini(block)) {
        block_t *block_prev = (block_t *)((char *)block - new_mini_block_size);
        if (!is_in_heap_boundary(block_prev)) {
            return NULL;
        }
        return block_prev;
    }

    word_t *footerp = find_prev_footer(block);

    // Return NULL if called on first block in the heap
    if (extract_size(*footerp) == 0) {
        return NULL;
    }

    return footer_to_header(footerp);
}

static block_t *find_free_block_next(block_t *block) {
    dbg_requires(block != NULL);
    free_pointers_t *fps = (free_pointers_t *)header_to_payload(block);
    return fps->next;
}

static block_t *find_mini_block_next(block_t *block) {
    dbg_requires(block != NULL);
    free_pointers_t *fps = (free_pointers_t *)header_to_payload(block);
    return fps->next;
}

static block_t *find_free_block_prev(block_t *block) {
    dbg_requires(block != NULL);
    free_pointers_t *fps = (free_pointers_t *)header_to_payload(block);
    return fps->prev;
}

static void link_free_block_next(block_t *block, block_t *next) {
    free_pointers_t *fps = (free_pointers_t *)header_to_payload(block);
    fps->next = next;
}

static void link_mini_block_next(block_t *block, block_t *next) {
    free_pointers_t *fps = (free_pointers_t *)header_to_payload(block);
    fps->next = next;
}

static void link_free_block_prev(block_t *block, block_t *prev) {
    free_pointers_t *fps = (free_pointers_t *)header_to_payload(block);
    fps->prev = prev;
}

static int search_root_index(size_t size) {
    size = max(size, min_block_size);

    int index = 0;
    size_t target_size = 1 << 5;
    while (index < (NUM_ROOTS - 1) && size > target_size) {
        index++;
        target_size = target_size << 1;
    }

    return index;
}

static void insert_mini_block(block_t *block) {
    dbg_requires(!get_alloc(block));
    dbg_requires(get_size(block) == new_mini_block_size);

    link_mini_block_next(block, mini_free_root);
    mini_free_root = block;
}

// LIFO
static void insert_free_block(block_t *block) {
    dbg_requires(!get_alloc(block));

    if (get_size(block) == new_mini_block_size) {
        insert_mini_block(block);
        return;
    }

    dbg_requires(get_size(block) >= min_block_size);

    int index = search_root_index(get_size(block));
    block_t *free_blocks_root = free_roots[index];

    link_free_block_next(block, free_blocks_root);
    link_free_block_prev(block, NULL);

    if (free_blocks_root) {
        link_free_block_prev(free_blocks_root, block);
    }
    free_roots[index] = block;
}

static void remove_mini_block(block_t *block) {
    dbg_requires(block != NULL);
    dbg_requires(!get_alloc(block));
    dbg_requires(get_size(block) == new_mini_block_size);

    block_t *block_current = mini_free_root;
    block_t *block_prev = NULL;
    while (block_current && block_current != block) {
        block_prev = block_current;
        block_current = find_mini_block_next(block_current);
    }
    if (!block_current) {
        return;
    }
    if (block_prev) {
        block_t *block_next = find_mini_block_next(block_current);
        link_mini_block_next(block_prev, block_next);
    } else {
        block_t *block_next = find_mini_block_next(block_current);
        mini_free_root = block_next;
    }

    link_mini_block_next(block_current, NULL);
}

static block_t *pop_mini_block() {
    block_t *block = mini_free_root;
    if (!block) {
        return NULL;
    }
    mini_free_root = find_mini_block_next(block);
    link_mini_block_next(block, NULL);
    return block;
}

// LIFO
static void remove_free_block(block_t *block) {
    dbg_requires(block != NULL);
    dbg_requires(!get_alloc(block));

    if (get_size(block) == new_mini_block_size) {
        remove_mini_block(block);
        return;
    }

    block_t *block_next = find_free_block_next(block);
    block_t *block_prev = find_free_block_prev(block);
    if (block_prev) {
        link_free_block_next(block_prev, block_next);
    } else {
        int index = search_root_index(get_size(block));
        free_roots[index] = block_next;
    }
    if (block_next) {
        link_free_block_prev(block_next, block_prev);
    }
    link_free_block_next(block, NULL);
    link_free_block_prev(block, NULL);
}

static bool is_addr_aligned(void *pointer) {
    return ((uintptr_t)pointer % dsize) == 0;
}

static void get_bucket_range(int index, size_t *low, size_t *high) {
    size_t base = 1 << 5;
    *low = base << (size_t)index;
    if (index == NUM_ROOTS - 1) {
        *high = (size_t)-1;
    } else {
        *high = base << 1;
    }
}

/*
 * ---------------------------------------------------------------------------
 *                        END SHORT HELPER FUNCTIONS
 * ---------------------------------------------------------------------------
 */

/******** The remaining content below are helper and debug routines ********/

/**
 * @brief Coalesce a newly-freed block with its free neighbors.
 *
 *  The function handles all 4 cases:
 *   1) prev alloc, next alloc → no merge
 *   2) prev alloc, next free  → merge with next
 *   3) prev free,  next alloc → merge with prev
 *   4) prev free,  next free  → merge with both
 *
 * @param[in] block  Pointer to a free block that is checked for coalesce.
 * @return Pointer to the free block after coalescing.
 *
 */
static block_t *coalesce_block(block_t *block) {
    /*
     *
     * Before you start, it will be helpful to review the "Dynamic Memory
     * Allocation: Basic" lecture, and especially the four coalescing
     * cases that are described.
     *
     * The actual content of the function will probably involve a call to
     * find_prev(), and multiple calls to write_block(). For examples of how
     * to use write_block(), take a look at split_block().
     *
     * Please do not reference code from prior semesters for this, including
     * old versions of the 213 website. We also discourage you from looking
     * at the malloc code in CS:APP and K&R, which make heavy use of macros
     * and which we no longer consider to be good style.
     */
    block_t *block_next = find_next(block);
    block_t *block_prev = find_prev(block);
    bool block_next_alloc =
        (get_size(block_next) == 0) ? 1 : get_alloc(block_next);
    bool block_prev_alloc = get_prev_alloc(block);
    bool block_prev_mini = get_prev_mini(block);

    // case 1
    if (block_prev_alloc && block_next_alloc) {
        size_t block_size = get_size(block);
        write_block(block, block_size, block_prev_mini, block_prev_alloc,
                    false);
        return block;
    }
    // case 2
    else if (block_prev_alloc && !block_next_alloc) {
        dbg_assert(!get_alloc(block_next));
        remove_free_block(block_next);
        size_t block_size = get_size(block) + get_size(block_next);
        write_block(block, block_size, block_prev_mini, block_prev_alloc,
                    false);
        return block;
    }
    // case 3
    else if (!block_prev_alloc && block_next_alloc) {
        dbg_assert(!get_alloc(block_prev));
        remove_free_block(block_prev);
        size_t block_size = get_size(block) + get_size(block_prev);
        write_block(block_prev, block_size, get_prev_mini(block_prev),
                    get_prev_alloc(block_prev), false);
        return block_prev;
    }
    // case 4
    else {
        dbg_assert(!get_alloc(block_prev));
        dbg_assert(!get_alloc(block_next));
        remove_free_block(block_prev);
        remove_free_block(block_next);
        size_t block_size =
            get_size(block_prev) + get_size(block) + get_size(block_next);
        write_block(block_prev, block_size, get_prev_mini(block_prev),
                    get_prev_alloc(block_prev), false);
        return block_prev;
    }
}

/**
 * @brief Request more memory from the heap.
 *
 * @param[in] size Minimum number of bytes to extend the heap.
 * @return Pointer to the free block from the extending result, 
 *         or NULL if mem_sbrk fails.
 */
static block_t *extend_heap(size_t size) {
    void *bp;

    // Allocate an even number of words to maintain alignment
    size = round_up(size, dsize);
    if ((bp = mem_sbrk((intptr_t)size)) == (void *)-1) {
        return NULL;
    }

    // Initialize free block header/footer
    block_t *block = payload_to_header(bp);
    bool prev_alloc = get_prev_alloc(block);
    bool prev_mini = get_prev_mini(block);
    write_block(block, size, prev_mini, prev_alloc, false);

    // Create new epilogue header
    block_t *block_next = find_next(block);
    write_epilogue(block_next, (get_size(block) == new_mini_block_size), false);

    // Coalesce in case the previous block was free
    block = coalesce_block(block);

    // insert new free block into segregated list
    insert_free_block(block);

    return block;
}

/**
 * @brief Split a just-allocated block if there is enough space.
 *
 * <What does this function do?>
 * <What are the function's arguments?>
 * <What is the function's return value?>
 * <Are there any preconditions or postconditions?>
 *
 * @param[in] block The block just being marked allocated.
 * @param[in] asize The desired allocated block size.
 */
static void split_block(block_t *block, size_t asize) {
    dbg_requires(get_alloc(block));
    /* TODO: Can you write a precondition about the value of asize? */

    size_t block_size = get_size(block);

    if ((block_size - asize) >= new_mini_block_size) {
        bool prev_alloc = get_prev_alloc(block);
        bool prev_mini = get_prev_mini(block);
        write_block(block, asize, prev_mini, prev_alloc, true);
        block_t *block_next;
        block_next = find_next(block);

        if ((block_size - asize) == new_mini_block_size) {
            write_block(block_next, new_mini_block_size,
                        (asize == new_mini_block_size), true, false);
        } else {
            write_block(block_next, block_size - asize,
                        (asize == new_mini_block_size), true, false);
        }
        insert_free_block(block_next);
    }

    dbg_ensures(get_alloc(block));
}

/**
 * @brief Find a free block that fits asize using segregated first-fit.
 *
 * @param[in] asize Requested size
 * @return Pointer to a fitting free block, or NULL if not exists.
 */
static block_t *find_fit(size_t asize) {
    for (int index = search_root_index(asize); index < NUM_ROOTS; index++) {
        block_t *free_blocks_root = free_roots[index];
        block_t *block;
        for (block = free_blocks_root; block != NULL;
             block = find_free_block_next(block)) {
            if (asize <= get_size(block)) {
                return block;
            }
        }
    }

    return NULL; // no fit found
}

/**
 * @brief Heap consistency checker.
 *
 * @param[in] line line number of the call site
 * @return true if all checks pass; false otherwise
 */
bool mm_checkheap(int line) {
    // check epilogue and prologue
    word_t *prologue_footer = (word_t *)mem_heap_lo();
    if (*prologue_footer != pack(0, false, true, true)) {
        fprintf(stderr,
                "Check for the prologue block fails (called at line %d)\n",
                line);
        return false;
    }
    word_t *epilogue_header = (word_t *)mem_heap_hi();
    if (*epilogue_header != pack(0, false, true, true)) {
        fprintf(stderr,
                "Check for the epilogue block fails (called at line %d)\n",
                line);
        return false;
    }

    // check all blocks
    size_t num_free_block_by_implicist_list = 0;
    for (block_t *block = heap_start, *block_prev = NULL; get_size(block) != 0; block_prev = block, block = find_next(block)) {
        // heap boundary
        if (!is_in_heap_boundary(block)) {
            fprintf(stderr, "Block lies out of heap. (called at line %d)\n",
                    line);
            return false;
        }

        // address alignment
        if (!is_addr_aligned(block)) {
            fprintf(stderr, "Block's address misaligned. (called at line %d)\n", line);
            return false;
        }

        // minimum size and size alignment checking
        size_t block_size = get_size(block);
        if ((block_size % dsize) != 0) {
            fprintf(stderr, "Found block size [%zu] is not divisible by %zu. (call at line %d)\n", block_size, dsize, line);
            return false;
        }
        if (!get_alloc(block) && block_size < min_block_size) {
            fprintf(stderr, "Found free block in size < min_block_size. (called at line %d)\n", line);
            return false;
        }

        if (!get_alloc(block)) {
            if (block_size != new_mini_block_size && block_size <
            min_block_size) {
                fprintf(stderr, "Free block size too small (not mini): %zu\n", block_size); 
                return false;
            }
        }

        // header/footer mtaching checks
        if (!get_alloc(block) && get_size(block) != new_mini_block_size) {
            word_t *footer = header_to_footer(block);
            if (*footer != pack(block_size, get_prev_mini(block), get_prev_alloc(block),
            get_alloc(block))) {
                fprintf(stderr, "Header and footer mismatch. (called at line %d)\n", line);
                return false;
            }
        }

        // coalescing checks
        if (block_prev && !get_alloc(block_prev) && !get_alloc(block)) {
            fprintf(stderr, "Found consecutive free blocks in the heap. (called at line %d)\n", line);
            return false;
        }

        if (!get_alloc(block)) {
            num_free_block_by_implicist_list++;
        }
    }

    // check all free blocks
    size_t num_free_block_by_segregated_list = 0;
    for (int i = 0; i < NUM_ROOTS; i++) {
        size_t low_bound;
        size_t high_bound;
        get_bucket_range(i, &low_bound, &high_bound);

        for (block_t *block = free_roots[i]; block != NULL;
             block = find_free_block_next(block)) {
            // heap boundary
            if (!is_in_heap_boundary(block)) {
                fprintf(stderr, "Free block lies out of heap. (called at line %d)\n", line);
                return false;
            }

            // address alignment
            if (!is_addr_aligned(block)) {
                fprintf(
                    stderr,
                    "Free block's address misaligned. (called at line %d)\n",
                    line);
                return false;
            }

            // bucket size range
            size_t block_size = get_size(block);
            if (!(block_size >= low_bound && block_size <= high_bound)) {
                fprintf(stderr,
                        "Free block size %zu out of bucket[%d]'s range "
                        "[%zu,%zu). (called at line %d)\n",
                        block_size, i, low_bound, high_bound, line);
                return false;
            }

            // next/previous pointers consistency
            block_t *block_next = find_free_block_next(block);
            block_t *block_prev = find_free_block_prev(block);
            if (block_next && find_free_block_prev(block_next) != block) {
                fprintf(stderr,
                        "next->previous pointers consistency. (called at line %d)\n",
                        line);
                return false;
            }
            if (block_prev && find_free_block_next(block_prev) != block) {
                fprintf(stderr,
                        "previous->next pointers consistency. (called at line %d)\n",
                        line);
                return false;
            }
            num_free_block_by_segregated_list++;
        }
    }

    if (num_free_block_by_implicist_list !=
    num_free_block_by_segregated_list) {
        fprintf(stderr,
                "free blocks count mismatch: by_implicist_list=%zu by_segregated_list=%zu (line %d)\n",
                num_free_block_by_implicist_list,
                num_free_block_by_segregated_list, line);
        return false;
    }

    return true;
}

/**
 * @brief Initialize and create an empty heap.
 *
 * @return true on success; false if mem_sbrk fails
 */
bool mm_init(void) {
    // init the segregated list
    for (int i = 0; i < NUM_ROOTS; i++) {
        free_roots[i] = NULL;
    }

    // Create the initial empty heap
    word_t *start = (word_t *)(mem_sbrk(2 * wsize));

    if (start == (void *)-1) {
        return false;
    }

    start[0] = pack(0, false, true, true); // Heap prologue (block footer)
    start[1] = pack(0, false, true, true); // Heap epilogue (block header)

    // Heap starts with first "block header", currently the epilogue
    heap_start = (block_t *)&(start[1]);

    // Extend the empty heap with a free block of chunksize bytes
    if (extend_heap(chunksize) == NULL) {
        return false;
    }

    return true;
}

/**
 * @brief Allocate a block with size bytes of payload.
 *
 *
 * @param[in] size Requested payload size in bytes.
 * @return Pointer to the payload of the allocated block, or NULL if fails.
 */
void *malloc(size_t size) {
    dbg_requires(mm_checkheap(__LINE__));

    size_t asize;      // Adjusted block size
    size_t extendsize; // Amount to extend heap if no fit is found
    block_t *block;
    void *bp = NULL;

    // Initialize heap if it isn't initialized
    if (heap_start == NULL) {
        if (!(mm_init())) {
            dbg_printf("Problem initializing heap. Likely due to sbrk");
            return NULL;
        }
    }

    // Ignore spurious request
    if (size == 0) {
        dbg_ensures(mm_checkheap(__LINE__));
        return bp;
    }

    // Adjust block size to include overhead and to meet alignment requirements
    asize = max(round_up(size + wsize, dsize), new_mini_block_size);

    if (asize == new_mini_block_size) {
        block = pop_mini_block();
        if (block) {
            write_block(block, new_mini_block_size, get_prev_mini(block),
                        get_prev_alloc(block), true);
            bp = header_to_payload(block);
            dbg_ensures(mm_checkheap(__LINE__));
            return bp;
        }
    }

    // Search the free list for a fit
    block = find_fit(asize);
    // If no fit is found, request more memory, and then and place the block
    if (block == NULL) {
        // Always request at least chunksize
        extendsize = max(asize, chunksize);
        block = extend_heap(extendsize);
        // extend_heap returns an error
        if (block == NULL) {
            return bp;
        }
    }

    // The block should be marked as free
    dbg_assert(!get_alloc(block));

    // remove the finding free block from the segregated list
    remove_free_block(block);

    // Mark block as allocated
    size_t block_size = get_size(block);
    bool prev_alloc = get_prev_alloc(block);
    bool prev_mini = get_prev_mini(block);
    write_block(block, block_size, prev_mini, prev_alloc, true);

    // Try to split the block if too large
    split_block(block, asize);

    bp = header_to_payload(block);

    dbg_ensures(mm_checkheap(__LINE__));
    return bp;
}

/**
 * @brief Free a previously allocated block.
 *
 * @param[in] bp Pointer previously returned by malloc/calloc/realloc, or NULL
 * 
 */
void free(void *bp) {
    dbg_requires(mm_checkheap(__LINE__));

    if (bp == NULL) {
        return;
    }

    block_t *block = payload_to_header(bp);
    size_t size = get_size(block);

    dbg_assert(get_size(block) >= new_mini_block_size);
    // The block should be marked as allocated
    dbg_assert(get_alloc(block));

    // Mark the block as free
    write_block(block, size, get_prev_mini(block), get_prev_alloc(block),
                false);

    // Try to coalesce the block with its neighbors
    block = coalesce_block(block);

    // insert the free block back to the segregated list
    insert_free_block(block);

    dbg_ensures(mm_checkheap(__LINE__));
}

/**
 * @brief Resize an allocated block while preserving existing data.
 *
 * @param[in] ptr Pointer to an existing allocation (or NULL).
 * @param[in] size Requested new payload size in bytes.
 * @return Pointer to the new allocation, or NULL on failure/size==0 
 */
void *realloc(void *ptr, size_t size) {
    block_t *block = payload_to_header(ptr);
    size_t copysize;
    void *newptr;

    // If size == 0, then free block and return NULL
    if (size == 0) {
        free(ptr);
        return NULL;
    }

    // If ptr is NULL, then equivalent to malloc
    if (ptr == NULL) {
        return malloc(size);
    }

    // Otherwise, proceed with reallocation
    newptr = malloc(size);

    // If malloc fails, the original block is left untouched
    if (newptr == NULL) {
        return NULL;
    }

    // Copy the old data
    copysize = get_payload_size(block); // gets size of old payload
    if (size < copysize) {
        copysize = size;
    }
    memcpy(newptr, ptr, copysize);

    // Free the old block
    free(ptr);

    return newptr;
}

/**
 * @brief Allocate zero-initialized memory
 *
 * @param[in] elements Number of elements.
 * @param[in] size Size of each element in bytes.
 * @return Pointer to zero-initialized memory, or NULL if (overflow is detected | allocation fails).
 */
void *calloc(size_t elements, size_t size) {
    void *bp;
    size_t asize = elements * size;

    if (elements == 0) {
        return NULL;
    }
    if (asize / elements != size) {
        // Multiplication overflowed
        return NULL;
    }

    bp = malloc(asize);
    if (bp == NULL) {
        return NULL;
    }

    // Initialize all bits to 0
    memset(bp, 0, asize);

    return bp;
}

/*
 *****************************************************************************
 * Do not delete the following super-secret(tm) lines!                       *
 *                                                                           *
 * 53 6f 20 79 6f 75 27 72 65 20 74 72 79 69 6e 67 20 74 6f 20               *
 *                                                                           *
 * 66 69 67 75 72 65 20 6f 75 74 20 77 68 61 74 20 74 68 65 20               *
 * 68 65 78 61 64 65 63 69 6d 61 6c 20 64 69 67 69 74 73 20 64               *
 * 6f 2e 2e 2e 20 68 61 68 61 68 61 21 20 41 53 43 49 49 20 69               *
 *                                                                           *
 * 73 6e 27 74 20 74 68 65 20 72 69 67 68 74 20 65 6e 63 6f 64               *
 * 69 6e 67 21 20 4e 69 63 65 20 74 72 79 2c 20 74 68 6f 75 67               *
 * 68 21 20 2d 44 72 2e 20 45 76 69 6c 0a c5 7c fc 80 6e 57 0a               *
 *                                                                           *
 *****************************************************************************
 */
