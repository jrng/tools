// sh_base.h - MIT License
// See end of file for full license

#ifndef __SH_BASE_INCLUDE__
#define __SH_BASE_INCLUDE__

#  define SH_PLATFORM_ANDROID 0
#  define SH_PLATFORM_FREEBSD 0
#  define SH_PLATFORM_WINDOWS 0
#  define SH_PLATFORM_LINUX   0
#  define SH_PLATFORM_MACOS   0

#  define SH_PLATFORM_UNIX    0

#  if defined(__ANDROID__)
#    undef  SH_PLATFORM_ANDROID
#    define SH_PLATFORM_ANDROID 1
#  elif defined(__FreeBSD__)
#    undef  SH_PLATFORM_FREEBSD
#    define SH_PLATFORM_FREEBSD 1
#  elif defined(_WIN32)
#    undef  SH_PLATFORM_WINDOWS
#    define SH_PLATFORM_WINDOWS 1
#  elif defined(__linux__)
#    undef  SH_PLATFORM_LINUX
#    define SH_PLATFORM_LINUX 1
#  elif defined(__APPLE__) && defined(__MACH__)
#    undef  SH_PLATFORM_MACOS
#    define SH_PLATFORM_MACOS 1
#  endif

#  if SH_PLATFORM_ANDROID || SH_PLATFORM_FREEBSD || SH_PLATFORM_LINUX || SH_PLATFORM_MACOS
#    undef  SH_PLATFORM_UNIX
#    define SH_PLATFORM_UNIX 1
#  endif

#  include <assert.h>
#  include <stdarg.h>
#  include <stddef.h>
#  include <stdint.h>
#  include <stdbool.h>

#  if SH_PLATFORM_WINDOWS
// winsock2.h needs to be included before windows.h and as sh_base.h is the first header
// of these libraries to get included we have to do it here. Maybe we can come up with a
// workaround to not include this if you are not using sh_http_server.h.
#    include <winsock2.h>
#  endif

#  if defined(SH_STATIC) || defined(SH_BASE_STATIC)
#    define SH_BASE_DEF static
#  else
#    define SH_BASE_DEF extern
#  endif

typedef ptrdiff_t ssize;
typedef size_t usize;

#  define ShKiB(value) (     (value) * (usize) 1024)
#  define ShMiB(value) (ShKiB(value) * (usize) 1024)
#  define ShGiB(value) (ShMiB(value) * (usize) 1024)

#  define ShArrayCount(arr) (sizeof(arr)/sizeof((arr)[0]))
#  define ShOffsetOf(type, member) (usize) &((type *) NULL)->member
#  define ShContainerOf(ptr, type, member) (type *) ((uint8_t *) (ptr) - ShOffsetOf(type, member))

typedef struct ShList ShList;

struct ShList
{
    ShList *prev;
    ShList *next;
};

#  define ShDListInit(sentinel)                      \
    (sentinel)->prev = (sentinel)->next = (sentinel);

#  define ShDListIsEmpty(sentinel)                   \
    ((sentinel)->prev == (sentinel))

#  define ShDListInsertBefore(sentinel, element)     \
    (element)->prev = (sentinel)->prev;              \
    (element)->next = (sentinel);                    \
    (sentinel)->prev->next = (element);              \
    (sentinel)->prev = (element);

#  define ShDListInsertAfter(sentinel, element)      \
    (element)->prev = (sentinel);                    \
    (element)->next = (sentinel)->next;              \
    (sentinel)->next->prev = (element);              \
    (sentinel)->next = (element);

#  define ShDListRemove(element)                     \
    (element)->prev->next = (element)->next;         \
    (element)->next->prev = (element)->prev;         \
    (element)->prev = (element)->next = (element);

typedef struct
{
    usize count;
    uint8_t *data;
} ShString;

#  define ShStringFmt ".*s"
#  define ShStringArg(str) (int) (str).count, (str).data

#  define ShStringConstant(str) { sizeof(str) - 1, (uint8_t *) (str) }

#  ifdef __cplusplus
#    define ShStringEmpty ShString {}
#    define ShStringLiteral(str) ShString { sizeof(str) - 1, (uint8_t *) (str) }
#    define ShCString(str) ShString { sh_c_string_get_length(str), (uint8_t *) (str) }
#  else
#    define ShStringEmpty (ShString) { 0, NULL }
#    define ShStringLiteral(str) (ShString) { sizeof(str) - 1, (uint8_t *) (str) }
#    define ShCString(str) (ShString) { sh_c_string_get_length(str), (uint8_t *) (str) }
#  endif

typedef struct
{
    uint32_t codepoint;
    uint32_t byte_count;
} ShUnicodeResult;

typedef enum
{
    SH_ALLOCATOR_ACTION_ALLOC   = 0,
    SH_ALLOCATOR_ACTION_REALLOC = 1,
    SH_ALLOCATOR_ACTION_FREE    = 2,
} ShAllocatorAction;

typedef void *(*ShAllocatorFunc)(void *allocator_data, ShAllocatorAction action, usize old_size, usize size, void *ptr);

typedef struct
{
    void *data;
    ShAllocatorFunc func;
} ShAllocator;

#  define sh_alloc_type(allocator, type) (type *) sh_alloc(allocator, sizeof(type))
#  define sh_alloc_array(allocator, type, count) (type *) sh_alloc(allocator, (count) * sizeof(type))

typedef struct
{
    ShAllocator allocator;
    usize saved_occupied;
} ShTemporaryMemory;

typedef struct
{
    uint8_t *base;
    usize capacity;
    usize occupied;
} ShArena;

typedef struct
{
    ShAllocator allocator;
    ShArena temporary_arenas[2];
} ShThreadContext;

typedef struct
{
    ShAllocator allocator;
    usize count;
    usize allocated;
} ShArrayHeader;

#  define sh_array_header(array) ((ShArrayHeader *) (array) - 1)
#  define sh_array_count(array) ((array) ? sh_array_header(array)->count : 0)
#  define sh_array_allocated(array) ((array) ? sh_array_header(array)->allocated : 0)
#  define sh_array_append(array) ((array) ? (sh_array_ensure_space(array), (array) + sh_array_header(array)->count++) : NULL)

#  ifdef __cplusplus
#    define sh_array_init(array, initial_allocated, allocator)  \
        ((array) = (decltype(+(array))) sh_array_grow(array, initial_allocated, sizeof(*(array)), allocator))
#    define sh_array_ensure_space(array) \
        (((sh_array_count(array) + 1) > sh_array_allocated(array)) ? \
            (array) = (decltype(+(array))) sh_array_grow(array, 2 * sh_array_allocated(array), sizeof(*(array)), ShAllocator { NULL, NULL }) : NULL)
#  else
#    define sh_array_init(array, initial_allocated, allocator)  \
        ((array) = sh_array_grow(array, initial_allocated, sizeof(*(array)), allocator))
#    define sh_array_ensure_space(array) \
        (((sh_array_count(array) + 1) > sh_array_allocated(array)) ? \
            (array) = sh_array_grow(array, 2 * sh_array_allocated(array), sizeof(*(array)), (ShAllocator) { NULL, NULL }) : NULL)
#  endif

SH_BASE_DEF void sh_arena_init_with_memory(ShArena *arena, void *memory, usize memory_size);
SH_BASE_DEF void sh_arena_allocate(ShArena *arena, usize capacity, ShAllocator allocator);
SH_BASE_DEF void sh_arena_clear(ShArena *arena);
SH_BASE_DEF void *sh_arena_alloc(ShArena *arena, usize size);
SH_BASE_DEF void *sh_arena_realloc(ShArena *arena, void *ptr, usize old_size, usize size);
SH_BASE_DEF ShAllocator sh_arena_get_allocator(ShArena *arena);

SH_BASE_DEF void *sh_array_grow(void *array, usize new_allocated, usize item_size, ShAllocator allocator);
SH_BASE_DEF void sh_array_free(void *array);

SH_BASE_DEF void *sh_alloc(ShAllocator allocator, usize size);
SH_BASE_DEF void *sh_realloc(ShAllocator allocator, void *ptr, usize old_size, usize size);
SH_BASE_DEF void sh_free(ShAllocator allocator, void *ptr);

SH_BASE_DEF ShThreadContext *sh_thread_context_create(ShAllocator allocator, usize temporary_memory_size);
SH_BASE_DEF void sh_thread_context_destroy(ShThreadContext *thread_context);

SH_BASE_DEF ShTemporaryMemory sh_begin_temporary_memory(ShThreadContext *thread_context, usize conflict_count, ShAllocator *conflicts);
SH_BASE_DEF void sh_end_temporary_memory(ShTemporaryMemory temporary_memory);

SH_BASE_DEF ShString sh_copy_string(ShAllocator allocator, ShString str);
SH_BASE_DEF char *sh_string_to_c_string(ShAllocator allocator, ShString str);

SH_BASE_DEF bool sh_string_equal(ShString a, ShString b);
SH_BASE_DEF bool sh_string_starts_with(ShString str, ShString prefix);
SH_BASE_DEF bool sh_string_ends_with(ShString str, ShString suffix);

SH_BASE_DEF ShString sh_string_concat_n(ShThreadContext *thread_context, ShAllocator allocator, usize n, ...);

SH_BASE_DEF ShString sh_string_trim(ShString str);
SH_BASE_DEF ShString sh_string_split_left(ShString *str, ShString split);
SH_BASE_DEF ShString sh_string_split_left_on_char(ShString *str, uint8_t c);
SH_BASE_DEF ShString sh_string_split_right(ShString *str, ShString split);
SH_BASE_DEF ShString sh_string_split_right_on_char(ShString *str, uint8_t c);

SH_BASE_DEF ShString sh_string_ascii_to_lower(ShAllocator allocator, ShString str);
SH_BASE_DEF ShString sh_string_ascii_to_upper(ShAllocator allocator, ShString str);

SH_BASE_DEF bool sh_parse_integer(ShString *str, int64_t *value);

SH_BASE_DEF usize sh_c_string_get_length(const char *str);

SH_BASE_DEF ShUnicodeResult sh_utf8_decode(ShString str, usize index);
SH_BASE_DEF usize sh_utf8_encode(ShString str, usize index, uint32_t codepoint);
SH_BASE_DEF ShUnicodeResult sh_utf16le_decode(ShString str, usize index);
SH_BASE_DEF usize sh_utf16le_encode(ShString str, usize index, uint32_t codepoint);
SH_BASE_DEF ShString sh_string_utf8_to_utf16le(ShAllocator allocator, ShString utf8_str);
SH_BASE_DEF ShString sh_string_utf16le_to_utf8(ShAllocator allocator, ShString utf16_str);

#endif // __SH_BASE_INCLUDE__

#ifdef SH_BASE_IMPLEMENTATION

SH_BASE_DEF void
sh_arena_init_with_memory(ShArena *arena, void *memory, usize memory_size)
{
    arena->base = (uint8_t *) memory;
    arena->capacity = memory_size;
    arena->occupied = 0;
}

SH_BASE_DEF void
sh_arena_allocate(ShArena *arena, usize capacity, ShAllocator allocator)
{
    arena->base = (uint8_t *) sh_alloc(allocator, capacity);
    arena->capacity = capacity;
    arena->occupied = 0;
}

SH_BASE_DEF void
sh_arena_clear(ShArena *arena)
{
    arena->occupied = 0;
}

SH_BASE_DEF void *
sh_arena_alloc(ShArena *arena, usize size)
{
    void *result = NULL;

    const usize alignment = 8;
    const usize alignment_mask = alignment - 1;

    usize alignment_offset = 0;
    usize next_pointer = (usize) (arena->base + arena->occupied);

    if (next_pointer & alignment_mask)
    {
        alignment_offset = alignment - (next_pointer & alignment_mask);
    }

    usize effective_size = size + alignment_offset;

    if ((arena->occupied + effective_size) <= arena->capacity)
    {
        result = arena->base + arena->occupied + alignment_offset;
        arena->occupied += effective_size;
    }

    return result;
}

SH_BASE_DEF void *
sh_arena_realloc(ShArena *arena, void *ptr, usize old_size, usize size)
{
    void *result = ptr;

    if (size > old_size)
    {
        // TODO: optimize: don't copy if right next to each other
        result = sh_arena_alloc(arena, size);

        if (result)
        {
            uint8_t *dst = (uint8_t *) result;
            uint8_t *src = (uint8_t *) ptr;

            while (old_size--) *dst++ = *src++;
        }
    }

    return result;
}

static void *
_sh_arena_allocator_func(void *allocator_data, ShAllocatorAction action, usize old_size, usize size, void *ptr)
{
    void *result = NULL;
    ShArena *arena = (ShArena *) allocator_data;

    switch (action)
    {
        case SH_ALLOCATOR_ACTION_ALLOC:   result = sh_arena_alloc(arena, size);                  break;
        case SH_ALLOCATOR_ACTION_REALLOC: result = sh_arena_realloc(arena, ptr, old_size, size); break;
        case SH_ALLOCATOR_ACTION_FREE:    /* there is no free for arena */                       break;
    }

    return result;
}

SH_BASE_DEF ShAllocator
sh_arena_get_allocator(ShArena *arena)
{
    ShAllocator allocator;
    allocator.data = arena;
    allocator.func = _sh_arena_allocator_func;
    return allocator;
}

SH_BASE_DEF void *
sh_array_grow(void *array, usize allocated, usize item_size, ShAllocator allocator)
{
    ShArrayHeader *array_header;

    if (array)
    {
        ShArrayHeader *old_array_header = sh_array_header(array);

        usize old_allocated = old_array_header->allocated;
        usize old_size = sizeof(ShArrayHeader) + (old_allocated * item_size);

        if (allocated > old_allocated)
        {
            usize size = sizeof(ShArrayHeader) + (allocated * item_size);

            array_header = (ShArrayHeader *) sh_realloc(old_array_header->allocator, old_array_header, old_size, size);
            array_header->allocated = allocated;
        }
        else
        {
            array_header = old_array_header;
        }
    }
    else
    {
        if (allocated < 4)
        {
            allocated = 4;
        }

        usize size = sizeof(ShArrayHeader) + (allocated * item_size);

        array_header = (ShArrayHeader *) sh_alloc(allocator, size);

        array_header->count = 0;
        array_header->allocated = allocated;
        array_header->allocator = allocator;
    }

    return array_header + 1;
}

SH_BASE_DEF void
sh_array_free(void *array)
{
    if (array)
    {
        ShArrayHeader *array_header = sh_array_header(array);
        sh_free(array_header->allocator, array_header);
    }
}

SH_BASE_DEF void *
sh_alloc(ShAllocator allocator, usize size)
{
    return allocator.func(allocator.data, SH_ALLOCATOR_ACTION_ALLOC, 0, size, NULL);
}

SH_BASE_DEF void *
sh_realloc(ShAllocator allocator, void *ptr, usize old_size, usize size)
{
    return allocator.func(allocator.data, SH_ALLOCATOR_ACTION_REALLOC, old_size, size, ptr);
}

SH_BASE_DEF void
sh_free(ShAllocator allocator, void *ptr)
{
    allocator.func(allocator.data, SH_ALLOCATOR_ACTION_FREE, 0, 0, ptr);
}

SH_BASE_DEF ShThreadContext *
sh_thread_context_create(ShAllocator allocator, usize temporary_memory_size)
{
    usize thread_context_size = sizeof(ShThreadContext);
    usize allocation_size = thread_context_size + (ShArrayCount(((ShThreadContext *) NULL)->temporary_arenas) * temporary_memory_size);

    uint8_t *allocation = (uint8_t *) sh_alloc(allocator, allocation_size);

    ShThreadContext *thread_context = (ShThreadContext *) allocation;
    allocation += thread_context_size;

    thread_context->allocator = allocator;

    for (usize i = 0; i < ShArrayCount(thread_context->temporary_arenas); i += 1)
    {
        sh_arena_init_with_memory(thread_context->temporary_arenas + i, allocation, temporary_memory_size);
        allocation += temporary_memory_size;
    }

    return thread_context;
}

SH_BASE_DEF void
sh_thread_context_destroy(ShThreadContext *thread_context)
{
    sh_free(thread_context->allocator, thread_context);
}

SH_BASE_DEF ShTemporaryMemory
sh_begin_temporary_memory(ShThreadContext *thread_context, usize conflict_count, ShAllocator *conflicts)
{
    ShTemporaryMemory temporary_memory;
    temporary_memory.allocator.data = NULL;
    temporary_memory.allocator.func = NULL;
    temporary_memory.saved_occupied = 0;

    ShArena *temporary_arena = NULL;

    for (usize i = 0; i < ShArrayCount(thread_context->temporary_arenas); i += 1)
    {
        ShArena *arena = thread_context->temporary_arenas + i;
        bool has_conflict = false;

        for (usize j = 0; j < conflict_count; j += 1)
        {
            if (arena == (ShArena *) conflicts[j].data)
            {
                has_conflict = true;
                break;
            }
        }

        if (!has_conflict)
        {
            temporary_arena = arena;
            break;
        }
    }

    if (temporary_arena)
    {
        temporary_memory.allocator = sh_arena_get_allocator(temporary_arena);
        temporary_memory.saved_occupied = temporary_arena->occupied;
    }

    return temporary_memory;
}

SH_BASE_DEF void
sh_end_temporary_memory(ShTemporaryMemory temporary_memory)
{
    ShArena *arena = (ShArena *) temporary_memory.allocator.data;
    assert(arena->occupied >= temporary_memory.saved_occupied);
    arena->occupied = temporary_memory.saved_occupied;
}

SH_BASE_DEF ShString
sh_copy_string(ShAllocator allocator, ShString str)
{
    ShString result = ShStringEmpty;

    if (str.count > 0)
    {
        result.count = str.count;
        result.data  = sh_alloc_array(allocator, uint8_t, result.count);

        uint8_t *src = str.data;
        uint8_t *dst = result.data;

        while (str.count--)
        {
            *dst++ = *src++;
        }
    }

    return result;
}

SH_BASE_DEF char *
sh_string_to_c_string(ShAllocator allocator, ShString str)
{
    char *result = sh_alloc_array(allocator, char, str.count + 2);

    for (size_t i = 0; i < str.count; i += 1)
    {
        result[i] = str.data[i];
    }

    result[str.count + 0] = 0;
    result[str.count + 1] = 0;

    return result;
}

SH_BASE_DEF bool
sh_string_equal(ShString a, ShString b)
{
    if (a.count != b.count)
    {
        return false;
    }

    for (usize i = 0; i < a.count; i += 1)
    {
        if (a.data[i] != b.data[i])
        {
            return false;
        }
    }

    return true;
}

SH_BASE_DEF bool
sh_string_starts_with(ShString str, ShString prefix)
{
    if (str.count < prefix.count)
    {
        return false;
    }

    ShString start;
    start.count = prefix.count;
    start.data  = str.data;

    return sh_string_equal(start, prefix);
}

SH_BASE_DEF bool
sh_string_ends_with(ShString str, ShString suffix)
{
    if (str.count < suffix.count)
    {
        return false;
    }

    ShString end;
    end.count = suffix.count;
    end.data  = str.data + (str.count - suffix.count);

    return sh_string_equal(end, suffix);
}

SH_BASE_DEF ShString
sh_string_concat_n(ShThreadContext *thread_context, ShAllocator allocator, usize n, ...)
{
    ShTemporaryMemory temp_memory = sh_begin_temporary_memory(thread_context, 1, &allocator);

    ShString result = { 0, NULL };
    ShString *strings = sh_alloc_array(temp_memory.allocator, ShString, n);

    va_list args;
    va_start(args, n);

    for (usize i = 0; i < n; i += 1)
    {
        strings[i] = va_arg(args, ShString);
        result.count += strings[i].count;
    }

    va_end(args);

    result.data = sh_alloc_array(allocator, uint8_t, result.count);

    uint8_t *at = result.data;

    for (usize i = 0; i < n; i += 1)
    {
        ShString str = strings[i];

        for (usize j = 0; j < str.count; j += 1)
        {
            *at++ = str.data[j];
        }
    }

    sh_end_temporary_memory(temp_memory);

    return result;
}

SH_BASE_DEF ShString
sh_string_trim(ShString str)
{
    while (str.count && ((str.data[str.count - 1] == ' ') ||
                         (str.data[str.count - 1] == '\t') ||
                         (str.data[str.count - 1] == '\r') ||
                         (str.data[str.count - 1] == '\n')))
    {
        str.count -= 1;
    }

    while (str.count && ((str.data[0] == ' ') || (str.data[0] == '\t') ||
                         (str.data[0] == '\r') || (str.data[0] == '\n')))
    {
        str.count -= 1;
        str.data += 1;
    }

    return str;
}

SH_BASE_DEF ShString
sh_string_split_left(ShString *str, ShString split)
{
    if (str->count < split.count)
    {
        ShString result;
        result.count = str->count;
        result.data  = str->data;
        str->count   = 0;
        str->data   += result.count;
        return result;
    }

    usize index = 0;
    usize end = str->count - split.count;

    ShString start;
    start.count = split.count;

    while (index <= end)
    {
        start.data = str->data + index;

        if (sh_string_equal(start, split))
        {
            break;
        }

        index += 1;
    }

    ShString result;
    result.count = index;
    result.data  = str->data;

    if (index <= end)
    {
        str->count -= split.count;
        str->data  += split.count;
    }

    str->count -= index;
    str->data  += index;

    return result;
}

SH_BASE_DEF ShString
sh_string_split_left_on_char(ShString *str, uint8_t c)
{
    usize index = 0;

    while (index < str->count)
    {
        if (str->data[index] == c)
        {
            break;
        }

        index += 1;
    }

    ShString result;
    result.count = index;
    result.data  = str->data;

    if (index < str->count)
    {
        str->count -= 1;
        str->data  += 1;
    }

    str->count -= index;
    str->data  += index;

    return result;
}

SH_BASE_DEF ShString
sh_string_split_right(ShString *str, ShString split)
{
    if (str->count < split.count)
    {
        ShString result;
        result.count = str->count;
        result.data  = str->data;
        str->count   = 0;
        return result;
    }

    usize index = str->count - split.count + 1;

    ShString end;
    end.count = split.count;

    while (index > 0)
    {
        end.data = str->data + (index - 1);

        if (sh_string_equal(end, split))
        {
            break;
        }

        index -= 1;
    }

    usize diff = index + split.count - 1;

    ShString result;
    result.count = str->count - diff;
    result.data  = str->data + diff;

    str->count = index;

    if (index > 0)
    {
        str->count -= 1;
    }

    return result;
}

SH_BASE_DEF ShString
sh_string_split_right_on_char(ShString *str, uint8_t c)
{
    usize index = str->count;

    while (index > 0)
    {
        if (str->data[index - 1] == c)
        {
            break;
        }

        index -= 1;
    }

    ShString result;
    result.count = str->count - index;
    result.data = str->data + index;

    str->count = index;

    if (index > 0)
    {
        str->count -= 1;
    }

    return result;
}

SH_BASE_DEF ShString
sh_string_ascii_to_lower(ShAllocator allocator, ShString str)
{
    ShString result = ShStringEmpty;

    if (str.count > 0)
    {
        result.count = str.count;
        result.data  = sh_alloc_array(allocator, uint8_t, result.count);

        uint8_t *src = str.data;
        uint8_t *dst = result.data;

        while (str.count--)
        {
            uint8_t c = *src++;

            if ((c >= 'A') && (c <= 'Z'))
            {
                c |= 0x20;
            }

            *dst++ = c;
        }
    }

    return result;
}

SH_BASE_DEF ShString
sh_string_ascii_to_upper(ShAllocator allocator, ShString str)
{
    ShString result = ShStringEmpty;

    if (str.count > 0)
    {
        result.count = str.count;
        result.data  = sh_alloc_array(allocator, uint8_t, result.count);

        uint8_t *src = str.data;
        uint8_t *dst = result.data;

        while (str.count--)
        {
            uint8_t c = *src++;

            if ((c >= 'a') && (c <= 'z'))
            {
                c &= ~0x20;
            }

            *dst++ = c;
        }
    }

    return result;
}

SH_BASE_DEF bool
sh_parse_integer(ShString *str, int64_t *value)
{
    size_t index = 0;
    bool has_sign = false;

    if ((index < str->count) && (str->data[index] == '-'))
    {
        has_sign = true;
        index += 1;
    }

    if ((index >= str->count) || (str->data[index] < '0') || (str->data[index] > '9'))
    {
        return false;
    }

    *value = 0;

    while ((index < str->count) && (str->data[index] >= '0') && (str->data[index] <= '9'))
    {
        *value = (10 * *value) + (str->data[index] - '0');
        index += 1;
    }

    if (has_sign)
    {
        *value = -*value;
    }

    str->count -= index;
    str->data  += index;

    return true;
}

SH_BASE_DEF usize
sh_c_string_get_length(const char *str)
{
    usize result = 0;
    if (str)
    {
        while (*str++) result += 1;
    }
    return result;
}

SH_BASE_DEF ShUnicodeResult
sh_utf8_decode(ShString str, usize index)
{
    ShUnicodeResult result = { '?', 1 };

    if (index < str.count)
    {
        uint8_t c = str.data[index];

        if ((c & 0x80) == 0x00)
        {
            result.codepoint = (c & 0x7F);
        }
        else if (((c & 0xE0) == 0xC0) && ((index + 1) < str.count) &&
                 ((str.data[index + 1] & 0xC0) == 0x80))
        {
            result.codepoint = ((uint32_t) (c & 0x1F) << 6) |
                                (uint32_t) (str.data[index + 1] & 0x3F);
            result.byte_count = 2;
        }
        else if (((c & 0xF0) == 0xE0) && ((index + 2) < str.count) &&
                 ((str.data[index + 1] & 0xC0) == 0x80) &&
                 ((str.data[index + 2] & 0xC0) == 0x80))
        {
            result.codepoint = ((uint32_t) (c & 0x0F) << 12) |
                               ((uint32_t) (str.data[index + 1] & 0x3F) << 6) |
                                (uint32_t) (str.data[index + 2] & 0x3F);
            result.byte_count = 3;
        }
        else if (((c & 0xF8) == 0xF0) && ((index + 3) < str.count) &&
                 ((str.data[index + 1] & 0xC0) == 0x80) &&
                 ((str.data[index + 2] & 0xC0) == 0x80) &&
                 ((str.data[index + 3] & 0xC0) == 0x80))
        {
            result.codepoint = ((uint32_t) (c & 0x0F) << 18) |
                               ((uint32_t) (str.data[index + 1] & 0x3F) << 12) |
                               ((uint32_t) (str.data[index + 2] & 0x3F) << 6) |
                                (uint32_t) (str.data[index + 3] & 0x3F);
            result.byte_count = 4;
        }
    }

    return result;
}

SH_BASE_DEF usize
sh_utf8_encode(ShString str, usize index, uint32_t codepoint)
{
    if (codepoint < 0x80)
    {
        if (index < str.count)
        {
            str.data[index] = (uint8_t) codepoint;

            return 1;
        }
    }
    else if (codepoint < 0x800)
    {
        if ((index + 1) < str.count)
        {
            str.data[index + 0] = 0xC0 | (uint8_t) ((codepoint >>  6) & 0x1F);
            str.data[index + 1] = 0x80 | (uint8_t) ( codepoint        & 0x3F);

            return 2;
        }
    }
    else if (codepoint < 0x10000)
    {
        if ((index + 2) < str.count)
        {
            str.data[index + 0] = 0xE0 | (uint8_t) ((codepoint >> 12) & 0x0F);
            str.data[index + 1] = 0x80 | (uint8_t) ((codepoint >>  6) & 0x3F);
            str.data[index + 2] = 0x80 | (uint8_t) ( codepoint        & 0x3F);

            return 3;
        }
    }
    else if (codepoint < 0x110000)
    {
        if ((index + 3) < str.count)
        {
            str.data[index + 0] = 0xF0 | (uint8_t) ((codepoint >> 18) & 0x07);
            str.data[index + 1] = 0x80 | (uint8_t) ((codepoint >> 12) & 0x3F);
            str.data[index + 2] = 0x80 | (uint8_t) ((codepoint >>  6) & 0x3F);
            str.data[index + 3] = 0x80 | (uint8_t) ( codepoint        & 0x3F);

            return 4;
        }
    }

    return 0;
}

SH_BASE_DEF ShUnicodeResult
sh_utf16le_decode(ShString str, usize index)
{
    ShUnicodeResult result = { '?', 2 };

    if ((index + 1) < str.count)
    {
        uint16_t leading = (str.data[index + 1] << 8) | str.data[index + 0];

        if (((leading & 0xFC00) == 0xD800) && ((index + 3) < str.count))
        {
            uint16_t trailing = (str.data[index + 3] << 8) | str.data[index + 2];

            result.codepoint = (((uint32_t) (leading & 0x3FF) << 10) |
                                 (uint32_t) (trailing & 0x3FF)) + 0x10000;
            result.byte_count = 4;
        }
        else
        {
            result.codepoint = leading;
            result.byte_count = 2;
        }
    }

    return result;
}

SH_BASE_DEF usize
sh_utf16le_encode(ShString str, usize index, uint32_t codepoint)
{
    if ((codepoint < 0xD800) || ((codepoint >= 0xE000) && (codepoint < 0x10000)))
    {
        if ((index + 1) < str.count)
        {
            str.data[index + 0] = (uint8_t) ( codepoint       & 0xFF);
            str.data[index + 1] = (uint8_t) ((codepoint >> 8) & 0xFF);
        }

        return 2;
    }
    else if ((codepoint >= 0x10000) && (codepoint < 0x110000))
    {
        if ((index + 3) < str.count)
        {
            codepoint -= 0x10000;
            uint32_t leading  = 0xD800 | ((codepoint >> 10) & 0x3FF);
            uint32_t trailing = 0xDC00 | ( codepoint        & 0x3FF);

            str.data[index + 0] = (uint8_t) ( leading       & 0xFF);
            str.data[index + 1] = (uint8_t) ((leading >> 8) & 0xFF);
            str.data[index + 2] = (uint8_t) ( trailing       & 0xFF);
            str.data[index + 3] = (uint8_t) ((trailing >> 8) & 0xFF);
        }

        return 4;
    }

    return 0;
}

SH_BASE_DEF ShString
sh_string_utf8_to_utf16le(ShAllocator allocator, ShString utf8_str)
{
    ShString result = ShStringEmpty;

    if (utf8_str.count)
    {
        result.count = 2 * utf8_str.count;
        result.data = sh_alloc_array(allocator, uint8_t, result.count);

        usize src_index = 0;
        usize dst_index = 0;

        while (src_index < utf8_str.count)
        {
            ShUnicodeResult utf8 = sh_utf8_decode(utf8_str, src_index);
            dst_index += sh_utf16le_encode(result, dst_index, utf8.codepoint);
            src_index += utf8.byte_count;
        }

        assert(dst_index <= result.count);

        result.count = dst_index;
    }

    return result;
}

SH_BASE_DEF ShString
sh_string_utf16le_to_utf8(ShAllocator allocator, ShString utf16_str)
{
    ShString result = ShStringEmpty;

    if (utf16_str.count)
    {
        result.count = 2 * utf16_str.count;
        result.data = sh_alloc_array(allocator, uint8_t, result.count);

        usize src_index = 0;
        usize dst_index = 0;

        while (src_index < utf16_str.count)
        {
            ShUnicodeResult utf16 = sh_utf16le_decode(utf16_str, src_index);
            dst_index += sh_utf8_encode(result, dst_index, utf16.codepoint);
            src_index += utf16.byte_count;
        }

        assert(dst_index <= result.count);

        result.count = dst_index;
    }

    return result;
}

#endif // SH_BASE_IMPLEMENTATION

/*
MIT License

Copyright (c) 2025 Julius Range-Lüdemann

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/
