/*
 * ==================================================
 * my own header toolbox.
 * inspired by stb.h from Sean Barrett(github.com/nothings)
 * ==================================================
 * */

#ifndef FUZZY_MY_H
#define FUZZY_MY_H

#if defined(__cplusplus)
extern "C" {
#endif

/*
 * ==================================================
 * Platforms.
 * ==================================================
 * */

#if defined(_WIN32) || defined(_WIN64)
    #define fz_OS_WINDOWS 1

#elif defined(__unix) || defined(__unix__)
    #define fz_OS_UNIX 1

    #if defined(__linux__)
        #define fz_OS_LINUX 1
    #elif defined(__FreeBSD__)
        #define fz_OS_FREEBSD 1
    #endif
#else
#error "This platform is not supported."
#endif


#if defined(__GNUC__)
    #define fz_COMPILER_GCC 1
#elif defined(__clang__)
    #define fz_COMPILER_CLANG 1
#elif defined(_MSC_VER)
    #define fz_COMPILER_MSVC 1
#else
    #error "unknown compiler."
#endif

#ifndef fz_DEF
#ifdef fz_STATIC_COMPILE
#define fz_DEF static
#else
#define fz_DEF extern
#endif
#endif

/*
 * ==================================================
 * Headers.
 * ==================================================
 * */

#if defined(fz_OS_WINDOWS) && !defined(fz_NO_WINDOWS_H) // raylib gets upset so I need another conditioning
#define NOMINMAX            1
#define WIN32_LEAN_AND_MEAN 1
#define VC_EXTRALEAN        1
#include <windows.h>

#define fz_WIN_H_INCLUDED 1
#endif

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <inttypes.h>

#include <stdlib.h> // for malloc, realloc, free
#include <stdio.h>  // printf
#include <string.h> // size_t
#include <assert.h> // assertion

/*
 * ==================================================
 * Bunch of basic defines.
 * ==================================================
 * */

#define fz_Introspect(...) // Used for metaprogramming. has no meaning on its own.

#define fz_COUNTOF(a) (sizeof(a)/sizeof(0[a]))

#define _fz_CONCAT1(a, b) a##b
#define _fz_STR1(n) #n

#define fz_CONCAT(a, b)   _fz_CONCAT1(a, b)
#define fz_STR(n)         _fz_STR1(n)

#define fz_FILE_AND_LINE "(" __FILE__ ":" fz_STR(__LINE__) ")"
#define fz_UNREACHABLE_PATH assert(false && "Unreachable Path Detected!!\n")

#define fz_UNUSED(x) ((void)x)

#define fz_STATIC_ASSERT(cond) \
  typedef char fz_CONCAT(fz_static_assert_failed_at_, __LINE__)[(cond) ? 1 : -1];

#define fz_KB ((size_t)1024)
#define fz_MB ((size_t)1024 * fz_KB)
#define fz_GB ((size_t)1024 * fz_MB)

#if !defined(fz_MINIMAL_FOOTPRINT)
/*
 * ==================================================
 * Memory Allocators
 * ==================================================
 * */

enum fz_Memory_Operation {
    fz_MEMORY_OPER_ALLOCATE,
    fz_MEMORY_OPER_FREE,
    fz_MEMORY_OPER_REALLOCATE,
};

//! @param op        operation.
//! @param ptr       non-null existing pointer for free, or realloc.
//! @param old_size  size for limitation on memmove when reallocating. only used for realloc, otherwise 0.
//! @param size      non-zero size for allocating new memory, or reallocating different size of memory.
//! @param user_data typically points to an allocator.
//! @return pointer to newly allocated memory, or NULL if free.
#define fz_OPER_FUNC(name) void *name(fz_Memory_Operation op, void *ptr, size_t old_size, size_t size, void *user_data)

typedef fz_OPER_FUNC(fz_Oper_Func);

struct fz_Allocator {
    void          *user_data;
    fz_Oper_Func *oper_func;
};

extern fz_Allocator fz_global_allocator;
extern fz_Allocator fz_global_temp_allocator;

fz_DEF fz_Allocator fz_hook_at_alloc(fz_Allocator new_allocator);
fz_DEF fz_Allocator fz_set_temp_allocator(fz_Allocator new_allocator);

// ==========================
// allocation using global definitions.

#define fz_alloc(size) (fz_alloc_ex(fz_global_allocator, size))
#define fz_free(ptr) (fz_free_ex(fz_global_allocator, ptr))
#define fz_realloc(ptr, old_size, size) (fz_realloc_ex(fz_global_allocator, ptr, old_size, size))

#define fz_heapalloc(size) (fz_alloc_ex(fz_heap_allocator(), size))
#define fz_heapfree(ptr) (fz_free_ex(fz_heap_allocator(), ptr))
#define fz_heaprealloc(ptr, old_size, size) (fz_realloc_ex(fz_heap_allocator(), ptr, old_size, size))

#define fz_talloc(size) (fz_alloc_ex(fz_global_temp_allocator, size))
#define fz_tfree(ptr)   (fz_free_ex(fz_global_temp_allocator, ptr))

inline void *
fz_alloc_ex(fz_Allocator allocator, size_t size) {
    return allocator.oper_func(fz_MEMORY_OPER_ALLOCATE, 0, 0, size, allocator.user_data);
}

inline void
fz_free_ex(fz_Allocator allocator, void *ptr) {
    allocator.oper_func(fz_MEMORY_OPER_FREE, ptr, 0, 0, allocator.user_data);
}

inline void *
fz_realloc_ex(fz_Allocator allocator, void *ptr, size_t old_size, size_t size) {
    return allocator.oper_func(fz_MEMORY_OPER_REALLOCATE, ptr, old_size, size, allocator.user_data);
}

inline uintptr_t
fz_align_to_power_of_two(uintptr_t unaligned, size_t size) {
#if fz_INCLUDE_STANDARDS
    assert((size & (size - 1)) == 0);
#endif

    uintptr_t aligned = unaligned;
    uintptr_t modulo = unaligned & ((uintptr_t)size - 1);

    if (modulo != 0) {
        aligned += size - modulo;
    }

    return aligned;
}

/*
 * ==================================================
 * Stretch Buffer (vector).
 * basically the same as stb_ds.h (https://github.com/nothings/stb/blob/master/stb_ds.h)
 * ==================================================
 * */

// Note: this "Vec" does nothing other than just wrapping pointer with fancy syntax.
// you can still assign pointer to this and compiler wouldn't complain.

struct fz_Array_Header_Type {
    fz_Allocator allocator;
    int caps;
    int used;
};

typedef int (*fz_COMPARATOR_FUNC)(const void *a, const void *b);

fz_DEF void *fz__vec_create(size_t entry_type_size, size_t capacity, fz_Allocator allocator);
fz_DEF int  fz__vec_grow(void **output, fz_Array_Header_Type *array, size_t element_size, size_t grow_count);
fz_DEF void fz__vec_release(fz_Array_Header_Type *array_header);
fz_DEF void fz__vec_sort(void *array, size_t element_size, fz_COMPARATOR_FUNC comparator_func);

#define fz_Vec(type)           type *
#define fz_Vec_Header(array)   ((fz_Array_Header_Type *)(array) - 1)
#define fz_Vec_Length(array)   ((array) ? fz_Vec_Header(array)->used : 0)
#define fz_Vec_Capacity(array) ((array) ? fz_Vec_Header(array)->caps : 0)
#define fz_Vec_SetLength(array, len) ((array) && (fz_Vec_Length(array) < len) && (fz_Vec_Capacity(array) < len) ? (fz_Vec_Header(array)->used = len) : 0)

// Internals
#define _FV_GROW(arr, sz)  (fz__vec_grow((void **)&(arr), fz_Vec_Header(arr), sizeof(arr[0]), sz))
#define _FV_MAYBEGROW(arr, sz) ((arr) && (fz_Vec_Length(arr) == fz_Vec_Capacity(arr)) \
                               ? _FV_GROW(arr, sz) : 0)

#define fz_Vec_CreateEx(type, caps, allocator) (type *)fz__vec_create(sizeof(type), (caps), (allocator))
#define fz_Vec_Create(type, caps)              fz_Vec_CreateEx(type, caps, fz_global_allocator)
#define fz_Vec_Release(array)                  fz__vec_release(fz_Vec_Header(array))

#define fz_Vec_Push(array, object)             (_FV_MAYBEGROW(array, 1), (array)[fz_Vec_Header(array)->used++] = (object))
#define fz_Vec_Pop(array)                      ((fz_Vec_Header(array)->used--), ((array)[fz_Vec_Header(array)->used]))
#define fz_Vec_Clear(array)                    ((array) ? ((fz_Vec_Header(array)->used = 0), 1) : 0)
#define fz_Vec_Last(array)                     (array)[fz_Vec_Header(array)->used - 1]
#define fz_Vec_Remove_Ordered(arr, i)          (memmove(&(arr)[i], &(arr)[i + 1], sizeof(arr[0]) * (fz_Vec_Length(arr) - i)), fz_Vec_Header(arr)->used -= 1)
#define fz_Vec_Remove_Unordered(arr, i)        (arr[i] = fz_Vec_Pop(arr))
#define fz_Vec_Sort(array, comparator_func)    ((array) ? fz__vec_sort((void *)(array), sizeof(array[0]), (comparator_func)) : (void)0)

#if !defined(fz_STRETCH_BUFFER_NO_SHORTHAND)
#define Vec           fz_Vec
#define VecHeader     fz_Vec_Header
#define VecCreate     fz_Vec_Create
#define VecCreateEx   fz_Vec_CreateEx
#define VecClear      fz_Vec_Clear
#define VecRelease    fz_Vec_Release
#define VecPush       fz_Vec_Push
#define VecPop        fz_Vec_Pop
#define VecLen        fz_Vec_Length
#define VecCap        fz_Vec_Capacity
#define VecSort       fz_Vec_Sort
#define VecLast       fz_Vec_Last

#define VecRemoveN          fz_Vec_Remove_Ordered
#define VecRemoveUnorderedN fz_Vec_Remove_Unordered
#endif

/*
 * ==================================================
 *  Hashmap.
 *  TODO: not implemented yet.
 * ==================================================
 * */


#if 0
#define fz_Map(type)
#define fz_Map_Header(map)

#define fz_Map_CreateEx(type, caps, allocator)
#define fz_Map_Create(type, caps)
#define fz_Map_Release(map)

#define fz_Map_Has(map, key)
#define fz_Map_Put(map, key, item)
#define fz_Map_Get(map, key)
#define fz_Map_Delete(map, key)

#if !defined(fz_STRETCH_BUFFER_NO_SHORTHAND)

#define Map fz_Map

#define MapCreateEx fz_Map_CreateEx
#define MapCreate   fz_Map_Create
#define MapRelease  fz_Map_Release

#define MapObj2Key  fz_Map_ObjectToKey
#define MapChar2Key fz_Map_CharToKey

#define MapContains fz_Map_Has
#define MapSet      fz_Map_Put
#define MapGet      fz_Map_Get
#define MapDelete   fz_Map_Delete

#endif // if defined fz_STRETCH_BUFFER_NO_SHORTHAND
#endif // if 0

/*
 * ==================================================
 *  Platform / Nil Allocation.
 * ==================================================
 * */

fz_DEF void *fz_platform_alloc(size_t size);
fz_DEF void *fz_platform_realloc(void *ptr, size_t size);
fz_DEF void  fz_platform_free(void *ptr);

inline fz_OPER_FUNC(fz_nil_operation) {
    fz_UNUSED(op);
    fz_UNUSED(ptr);
    fz_UNUSED(size);
    fz_UNUSED(user_data);
    return NULL;
}


/*
 * ==================================================
 * Heap / Platform Allocator.
 * ==================================================
 * */

struct fz_Heap {
    int empty;
};

fz_OPER_FUNC(fz_heap_operation);
fz_DEF fz_Allocator fz_heap_allocator();

/*
 * ==================================================
 * Arena Allocator.
 * ==================================================
 * */

struct fz_Arena {
    uint8_t *memory;
    size_t   capacity;
    size_t   used;
};

struct fz_Temp_Memory {
    fz_Arena *arena;
    size_t used_before;
};

fz_DEF void fz_arena_init(fz_Arena *arena, void *backing_memory, size_t memory_size);

fz_DEF fz_OPER_FUNC(fz_arena_operation);

fz_DEF fz_Allocator   fz_arena_allocator(fz_Arena *arena);
fz_DEF fz_Temp_Memory fz_begin_temp(fz_Arena *arena);
fz_DEF void            fz_end_temp(fz_Temp_Memory scratch);


/*
 * ==================================================
 * Stack Allocator.
 * ==================================================
 * */

struct fz_Stack_Header {
    size_t prev_offset;
    size_t padding;
};

struct fz_StackAlloc {
    uint8_t *base;
    size_t   caps;

    // offsets.
    size_t   prev;
    size_t   current;
};

fz_DEF void fz_stack_init(fz_StackAlloc *stackalloc, void *backing_memory, size_t memory_size);
fz_DEF fz_Allocator fz_stack_allocator(fz_StackAlloc *stackalloc);

fz_DEF fz_OPER_FUNC(fz_stack_operation);


/*
 * ==================================================
 * Pool Allocator.
 * ==================================================
 * */

// TODO
struct fz_SLL_Header {
    fz_SLL_Header *next;
};

struct fz_Pool {
    uint8_t *base;
    size_t element_size;
    size_t memory_caps; // The total size of memory capacity. NOT the count of total usable chunk.
    fz_SLL_Header *free;
};

fz_DEF void fz_pool_init(fz_Pool *pool, void *backing_memory, size_t memory_size, size_t element_size);
fz_DEF fz_Allocator fz_pool_allocator(fz_Pool *pool);

fz_DEF fz_OPER_FUNC(fz_pool_operation);

/*
 * ==================================================
 * FreeList Allocator.
 * ==================================================
 * */

struct fz_SizeHeader {
    size_t size;
};

struct fz_ListNode {
    size_t size;
    fz_ListNode *prev;
    fz_ListNode *next;
};

struct fz_Freelist {
    void *base;
    size_t memory_caps;

    fz_ListNode sentinel;
};

fz_DEF void fz_freelist_init(fz_Freelist *freelist, void *backing_memory, size_t memory_size);
fz_DEF fz_Allocator fz_freelist_allocator(fz_Freelist *freelist);

fz_OPER_FUNC(fz_freelist_operation);

/*
 * ==================================================
 * Ring Allocator.
 * ==================================================
 * */

struct fz_Ring {
    void *base; // Fixed position -- never changes.
    size_t memory_size;

    void *alloc;
    void *free;
};

fz_OPER_FUNC(fz_ring_operation);
fz_DEF void fz_ring_init(fz_Ring *freelist, void *backing_memory, size_t memory_size);
fz_DEF fz_Allocator fz_ring_allocator(fz_Ring *ring);

#else  // if !defined(fz_MINIMAL_FOOTPRINT) {...above block...} else

fz_DEF void *xmalloc(size_t size);
fz_DEF void xfree(void *ptr);
fz_DEF void *xrealloc(void *ptr, size_t size);
fz_DEF void *xcalloc(size_t size, size_t count);

#endif // fz_MINIMAL_FOOTPRINT ( 113 )

#if defined(__cplusplus)
}
#endif

 // ==================================================
 // Everything below is a C++ feature.

#if defined(__cplusplus)
#if !defined(fz_MINIMAL_FOOTPRINT)
struct fz_Temp_Block {
    fz_Temp_Memory tm;

    fz_Temp_Block(fz_Arena &arena) {
        tm = fz_begin_temp(&arena);
    }

    ~fz_Temp_Block() {
        fz_end_temp(tm);
    }
};
#endif

/*
 * ==================================================
 * Scope Exit.
 * usage:
 *     some initializtaion code here;
 *     fz_scopeexit { something... };
 * ==================================================
 * */
template<typename T>
struct fz_ScopeExit {
    T func;
    fz_ScopeExit(T f): func(f) {}
    ~fz_ScopeExit() { func(); }
};

struct fz_ScopeExit_Help {
    template<typename G>
    fz_ScopeExit<G> operator+(G func) { return func; }
};

#define fz_scopeexit auto fz_CONCAT(FZ_SCOPEEXIT_, __LINE__) = fz_ScopeExit_Help() + [&]

#endif


#endif // FUZZY_MY_H

/*
 * ==================================================
 * Implementations.
 * ==================================================
 * */

#if defined(FUZZY_MY_H_IMPL) && !defined(FUZZY_MY_H_IMPLEMENTED)
#define FUZZY_MY_H_IMPLEMENTED 1

#if defined(__cplusplus)
extern "C" {
#endif

#if !defined(fz_MINIMAL_FOOTPRINT)

fz_Allocator fz_global_allocator = { 0, fz_heap_operation };
fz_Allocator fz_global_temp_allocator = { 0, fz_nil_operation };

fz_Allocator fz_set_allocator(fz_Allocator new_allocator) {
    fz_Allocator old = fz_global_allocator;
    fz_global_allocator = new_allocator;

    return old;
}

fz_Allocator fz_set_temp_allocator(fz_Allocator new_allocator) {
    fz_Allocator old = fz_global_temp_allocator;
    fz_global_temp_allocator = new_allocator;

    return old;
}

/*
 * ==================================================
 * Heap / Platform Allocator.
 * ==================================================
 * */

// ===============================================
// you can rewrite these functions to implement
// your very own platform allocation functions!!

void *fz_platform_alloc(size_t size) {
    assert(size > 0);
    void *result = malloc(size);

    assert(result && "Failed to allocate memory.");
    return result;
}

void *fz_platform_realloc(void *ptr, size_t size) {
    assert(size > 0);
    void *result = realloc(ptr, size);

    assert(result && "Failed to reallocate memory.");
    return result;
}

void fz_platform_free(void *ptr) {
    assert(ptr && "trying to free a pointer that is null.");
    free(ptr);
}

fz_OPER_FUNC(fz_heap_operation) {
    switch(op) {
        case fz_MEMORY_OPER_ALLOCATE:
            return fz_platform_alloc(size);

        case fz_MEMORY_OPER_FREE:
            fz_platform_free(ptr); return NULL;

        case fz_MEMORY_OPER_REALLOCATE:
            return fz_platform_realloc(ptr, size);
    }
    return NULL;
}

fz_Allocator fz_heap_allocator() {
    fz_Allocator allocator = {0};
    allocator.oper_func = fz_heap_operation;

    return allocator;
}

/*
 * ==================================================
 * Stretch Buffer (vector).
 * basically the same as stb_ds.h (https://github.com/nothings/stb/blob/master/stb_ds.h)
 * ==================================================
 * */

/*
 * Couple things to note:
 * @param output -- this points to a beginning of an array, AFTER the header.
 * @param header -- this points to a beginning of a header, which should equal to (output - sizeof(fz_Array_Header_Type)).
 * */
int fz__vec_grow(void **output, fz_Array_Header_Type *header, size_t element_size, size_t grow_count) {
    assert(header);
    assert(output);
    assert((*output) == (header + 1));
    if (header->caps == 0) header->caps = 1;

    int next_cap    = header->caps;
    int next_length = header->used + grow_count;

    while(next_cap <= next_length) next_cap *= 2;
    size_t old_size = (size_t) header->caps * element_size;
    size_t new_size = (size_t) next_cap     * element_size;

    fz_Array_Header_Type *new_array = (fz_Array_Header_Type *)fz_realloc_ex(header->allocator, header, old_size, new_size);
    assert(new_array);

    new_array->caps = next_cap;
    *output = (void *)(new_array + 1);
    return 1;
}

void *
fz__vec_create(size_t entry_type_size, size_t capacity, fz_Allocator allocator) {
    fz_Array_Header_Type *result = (fz_Array_Header_Type *)fz_alloc_ex(allocator, sizeof(fz_Array_Header_Type) + (entry_type_size * capacity));

    result->allocator = allocator;
    result->used      = 0;
    result->caps      = capacity;

    return (void *)(result + 1);
}

void fz__vec_release(fz_Array_Header_Type *array_header) {
    assert(array_header);
    fz_free_ex(array_header->allocator, array_header);
}

void fz__vec_sort(fz_Vec(void) array, size_t elem_size, fz_COMPARATOR_FUNC comparator_func) {
    qsort(array, fz_Vec_Length(array), elem_size, comparator_func);
}

/*
 * ==================================================
 * Arena Allocator.
 * ==================================================
 * */

#ifndef fz_PUSH_ALIGNMENT
#define fz_PUSH_ALIGNMENT 16
#endif

void fz_arena_init(fz_Arena *arena, void *backing_memory, size_t memory_size) {
    if (arena && backing_memory) {
        arena->capacity = memory_size;
        arena->used     = 0;
        arena->memory   = (uint8_t *)backing_memory;
        arena->capacity = memory_size;
    }
}

fz_OPER_FUNC(fz_arena_operation) {
    fz_Arena *arena = (fz_Arena *)user_data;

    int reallocating = 0;
    switch(op) {
        case fz_MEMORY_OPER_REALLOCATE:
            assert(arena->memory < ptr && ptr < (arena->memory + arena->capacity));
            reallocating = 1;
        /*
         * fallthrough.
         *  reallocate and allocate does the same thing, since it's cannot be freed
         */

        case fz_MEMORY_OPER_ALLOCATE:
        {
            if (size < fz_PUSH_ALIGNMENT) size = fz_PUSH_ALIGNMENT;

            if (reallocating) {
                assert(old_size <= arena->used);

                if (ptr == (arena->memory + (arena->used - old_size))) { // ptr is the last allocation point and can be simply extended.
                    // NOTE(fuzzy): I don't know why you would want to do that, but you can realloc memory to a smaller size.
                    // in that case do nothing.
                    if (old_size < size) {
                        size_t size_difference = size - old_size;
                        uintptr_t aligned_size = fz_align_to_power_of_two(size_difference, fz_PUSH_ALIGNMENT);
                        arena->used += aligned_size;
                    }
                    return ptr;
                }
            }

            uintptr_t unaligned_memory_ptr = (uintptr_t)(arena->memory + arena->used);
            uintptr_t memory_ptr = fz_align_to_power_of_two(unaligned_memory_ptr, fz_PUSH_ALIGNMENT);

            // This amount (remainder) will be pushed along with the size of allocation
            // to match the alignment. this will never go negative.
            ptrdiff_t remainder = memory_ptr - unaligned_memory_ptr;
            assert(remainder >= 0);
            assert((arena->used + remainder + size) < arena->capacity);

            uint8_t *memory = (uint8_t *)memory_ptr;
            arena->used += remainder + size;

            if (reallocating) {
                memmove(ptr, memory, old_size);
            }

            return memory;
        };

        // For the sake of completeness.
        case fz_MEMORY_OPER_FREE:
        {
            return NULL;
        };
    }
    return NULL;
}

fz_Allocator fz_arena_allocator(fz_Arena *arena) {
    fz_Allocator result;
    result.user_data = (void *)arena;
    result.oper_func = fz_arena_operation;
    return result;
}

fz_Temp_Memory fz_begin_temp(fz_Arena *arena) {
    fz_Temp_Memory result;
    result.arena = arena;
    result.used_before = arena->used;
    return result;
}

void fz_end_temp(fz_Temp_Memory scratch) {
    scratch.arena->used = scratch.used_before;
}

/*
 * ==================================================
 * Stack Allocator.
 * ==================================================
 * */

void fz_stack_init(fz_StackAlloc *stackalloc, void *backing_memory, size_t memory_size) {
    stackalloc->base = (uint8_t *)backing_memory;
    stackalloc->caps = memory_size;
    stackalloc->prev = 0;
    stackalloc->current = 0;
}

fz_Allocator fz_stack_allocator(fz_StackAlloc *stackalloc) {
    fz_Allocator result;
    result.user_data = (void *)stackalloc;
    result.oper_func = fz_stack_operation;
    return result;
}

fz_OPER_FUNC(fz_stack_operation) {
    fz_StackAlloc *stack = (fz_StackAlloc *)user_data;

    switch(op) {
        case fz_MEMORY_OPER_ALLOCATE:
        {
            size_t new_size = size + sizeof(fz_Stack_Header);
            uintptr_t unaligned_ptr = (uintptr_t)(stack->base + stack->current);
            uintptr_t memory_ptr = fz_align_to_power_of_two(unaligned_ptr, fz_PUSH_ALIGNMENT);

            ptrdiff_t remainder = memory_ptr - unaligned_ptr;
            assert(remainder >= 0);
            assert((stack->current + remainder + new_size) < stack->caps);

            fz_Stack_Header *memory = (fz_Stack_Header *)memory_ptr;

            memory->prev_offset = stack->prev;
            memory->padding     = remainder;

            stack->prev     = stack->current;
            stack->current += new_size + remainder;
            stack->caps    += new_size + remainder;

            return (void *)(memory + 1);
        } break;

        case fz_MEMORY_OPER_FREE:
        {
            assert(stack->base <= ptr && ptr <= (stack->base + stack->caps));
            fz_Stack_Header *header = (fz_Stack_Header *)ptr - 1;
            size_t header_placed_in = ((size_t)header - header->padding - (size_t)stack->base);
            assert(stack->prev == header_placed_in && "Order difference: stack free must follow LIFO rules.");

            stack->current = stack->prev;
            stack->prev = header->prev_offset;
            stack->caps -= stack->current - stack->prev;
        } break;

        case fz_MEMORY_OPER_REALLOCATE:
        {
            assert(stack->base <= ptr && ptr <= (stack->base + stack->caps));
            fz_Stack_Header *header = (fz_Stack_Header *)ptr - 1;
            size_t header_placed_in = ((size_t)header - header->padding - (size_t)stack->base);
            assert(stack->prev == header_placed_in && "Order difference: stack free must follow LIFO rules.");

            size_t allocation_size = stack->prev - stack->current;
            if (allocation_size < size) {
                stack->current += size - allocation_size;
            }

            return ptr;
        } break;
    }

    return NULL;
}

/*
 * ==================================================
 * Pool Allocator.
 * ==================================================
 * */

void fz_pool_init(fz_Pool *pool, void *backing_memory, size_t memory_size, size_t element_size) {
    assert(memory_size  > sizeof(fz_SLL_Header));
    assert(element_size > sizeof(fz_SLL_Header));

    uint8_t *backing = (uint8_t *)backing_memory;
    size_t available_count = memory_size / element_size; // any fractions will get rounded down to 0.
    memset(backing, 0, memory_size);

    // NOTE:
    // available_count - 1 means that the last one of the sll header
    // will be left resetted to zero from the memset above;
    // effectively leaving the next member variable to NULL
    for (int i = 0; i < available_count - 1; ++i) {
        size_t memory_position = i * element_size;
        fz_SLL_Header *current = (fz_SLL_Header *)(backing + memory_position);
        current->next = (fz_SLL_Header *)(backing + memory_position + element_size);
    }

    pool->base         = backing;
    pool->element_size = element_size;
    pool->memory_caps  = memory_size;
    pool->free         = (fz_SLL_Header *)backing;
}

fz_Allocator fz_pool_allocator(fz_Pool *pool) {
    fz_Allocator allocator;
    allocator.user_data = pool;
    allocator.oper_func = fz_pool_operation;

    return allocator;
}

fz_OPER_FUNC(fz_pool_operation) {
    fz_UNUSED(old_size);
    fz_Pool *pool = (fz_Pool *)user_data;

    switch(op) {
        case fz_MEMORY_OPER_ALLOCATE:
        {
            assert(size == pool->element_size);
            fz_SLL_Header *available_chunk = pool->free;

            if (pool->free)
                pool->free = pool->free->next;

            memset(available_chunk, 0, size);
            return (void *)available_chunk;
        };

        case fz_MEMORY_OPER_FREE:
        {
            assert(pool->base <= ptr && ptr < (pool->base + pool->memory_caps));
            memset(ptr, 0, pool->element_size);

            fz_SLL_Header freed;
            freed.next = pool->free;

            *(fz_SLL_Header *)ptr = freed;
            pool->free = (fz_SLL_Header *)ptr;

        } break;

        case fz_MEMORY_OPER_REALLOCATE:
        {
            fz_UNREACHABLE_PATH;
        };
    }

    return NULL;
}

/*
 * ==================================================
 * Freelist Allocator.
 * ==================================================
 * */

void fz_freelist_init(fz_Freelist *freelist, void *backing_memory, size_t memory_size) {
    freelist->base = backing_memory;
    freelist->memory_caps = memory_size;

    uintptr_t rounded_up = fz_align_to_power_of_two((uintptr_t)backing_memory, 16);
    fz_ListNode *node = (fz_ListNode*) rounded_up;
    node->size = memory_size - (ptrdiff_t)(rounded_up - (uintptr_t)backing_memory) - sizeof(fz_SizeHeader);

    freelist->sentinel.size = 0;
    freelist->sentinel.next = node;
    freelist->sentinel.prev = node;

    node->next = &freelist->sentinel;
    node->prev = &freelist->sentinel;
}

fz_Allocator fz_freelist_allocator(fz_Freelist *freelist) {
    fz_Allocator allocator;
    allocator.user_data = freelist;
    allocator.oper_func = fz_freelist_operation;
    return allocator;
}

fz_OPER_FUNC(fz_freelist_operation) {
    fz_Freelist *list = (fz_Freelist *)user_data;
    size_t size_pow2 = fz_align_to_power_of_two(size, 16);

    switch(op) {
        case fz_MEMORY_OPER_ALLOCATE:
        {
            fz_ListNode *current  = list->sentinel.next;
            fz_ListNode *best_fit = list->sentinel.next;

            while(current != &list->sentinel) {
                if (size_pow2 == current->size) {
                    best_fit = current;
                    break;
                }

                if (size_pow2 <= current->size && current->size < best_fit->size) {
                    best_fit = current;
                }
                current = current->next;
            }
            // Too big. fail.
            if (best_fit->size <= size_pow2) {
                return NULL;
            }

            if (best_fit->size >= (size_pow2 * 2)) {
                // Cut size.
                size_t fit_oldsize = best_fit->size;
                best_fit->size = size_pow2;
                fz_ListNode *remain_node = (fz_ListNode *)((char *)best_fit + sizeof(fz_SizeHeader) + size_pow2);

                remain_node->size = fit_oldsize - size_pow2 - sizeof(fz_SizeHeader);
                remain_node->next = best_fit->next;
                remain_node->prev = best_fit->prev;

                best_fit->next->prev = remain_node;
                best_fit->prev->next = remain_node;
            } else {
                best_fit->next->prev = best_fit->prev;
                best_fit->prev->next = best_fit->next;
            }

            fz_SizeHeader *header = (fz_SizeHeader *)best_fit;
            header->size = best_fit->size;
            return (void *)(header + 1);
        } break;

        case fz_MEMORY_OPER_FREE:
        {
            assert(list->base <= ptr && ptr < ((char *)list->base + list->memory_caps));

            fz_SizeHeader *header = ((fz_SizeHeader*)ptr - 1);
            size_t block_size = header->size;

            // Coalesce
            fz_ListNode *node = list->sentinel.next;
            fz_ListNode *coalesce = NULL;

            while(node != &list->sentinel) {
                if (node == (fz_ListNode *)((char *)ptr + block_size)) {
                    coalesce = node;
                    break;
                }
                node = node->next;
            }

            fz_ListNode *pushing = (fz_ListNode *)header;
            if (coalesce) {
                pushing->next = coalesce->next;
                pushing->prev = coalesce->prev;
                coalesce->next->prev = pushing;
                coalesce->prev->next = pushing;
                pushing->size = block_size + coalesce->size + sizeof(fz_SizeHeader);
            } else {
                pushing->size = block_size;
                pushing->next = list->sentinel.next;
                pushing->prev = list->sentinel.next->prev;
                list->sentinel.next = pushing;
            }
        } break;

        case fz_MEMORY_OPER_REALLOCATE:
        {
            size_t block_size = ((fz_SizeHeader*)ptr - 1)->size;
            assert(size > block_size);

            void *new_memory = fz_freelist_operation(fz_MEMORY_OPER_ALLOCATE, 0, 0, size, user_data);

            if (new_memory) {
                memmove(new_memory, ptr, block_size);
                fz_freelist_operation(fz_MEMORY_OPER_FREE, ptr, 0, 0, user_data);
                return new_memory;
            }

            return NULL;
        } break;
    }

    return NULL;
}

#else  // if !defined(fz_MINIMAL_FOOTPRINT) {...above block...} else

// xmalloc, xrealloc, xcalloc never returns 0.
// xfree catches NULL free / double free, and overwrites ptr to 0.

void *xmalloc(size_t size) {
    assert(size > 0);
    void *result = malloc(size);

    assert(result && "Failed to allocate memory.");
    return result;
}

void xfree(void *ptr) {
    assert(ptr && "trying to free a pointer that is null.");
    free(ptr);
}

void *xrealloc(void *ptr, size_t size) {
    assert(size > 0);
    void *result = realloc(ptr, size);

    assert(result && "Failed to reallocate memory.");
    return result;
}

void *xcalloc(size_t size, size_t count) {
    assert(size > 0 && count > 0);
    void *result = calloc(size, count);

    assert(result && "Failed to callocate memory.");
    return result;
}

#endif // fz_MINIMAL_FOOTPRINT

#if defined(__cplusplus)
}
#endif

#endif // FUZZY_MY_H_IMPL
