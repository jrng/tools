// sh_string_builder.h - MIT License
// See end of file for full license

#ifndef __SH_STRING_BUILDER_INCLUDE__
#define __SH_STRING_BUILDER_INCLUDE__

#  ifndef __SH_BASE_INCLUDE__
#    error "sh_string_builder.h requires sh_base.h to be included first"
#  endif

#  if defined(SH_STATIC) || defined(SH_STRING_BUILDER_STATIC)
#    define SH_STRING_BUILDER_DEF static
#  else
#    define SH_STRING_BUILDER_DEF extern
#  endif

typedef struct ShStringBuffer ShStringBuffer;

struct ShStringBuffer
{
    ShStringBuffer *next;
    usize occupied;
    uint8_t data[ShKiB(4)];
};

typedef struct
{
    ShAllocator allocator;
    ShStringBuffer *first_buffer;
    ShStringBuffer *last_buffer;
} ShStringBuilder;

SH_STRING_BUILDER_DEF void sh_string_builder_init(ShStringBuilder *builder, ShAllocator allocator);
SH_STRING_BUILDER_DEF usize sh_string_builder_get_size(ShStringBuilder *builder);
SH_STRING_BUILDER_DEF void sh_string_builder_append_u8(ShStringBuilder *builder, uint8_t c);
SH_STRING_BUILDER_DEF void sh_string_builder_append_string(ShStringBuilder *builder, ShString str);
SH_STRING_BUILDER_DEF void sh_string_builder_append_unsigned_number(ShStringBuilder *builder, uint64_t value,
                                                                    usize leading_character_count, uint8_t leading_character,
                                                                    uint64_t base, bool uppercase_digits);
SH_STRING_BUILDER_DEF void sh_string_builder_append_signed_number(ShStringBuilder *builder, int64_t value,
                                                                  usize leading_character_count, uint8_t leading_character,
                                                                  uint64_t base, bool uppercase_digits);
SH_STRING_BUILDER_DEF void sh_string_builder_append_formated(ShStringBuilder *builder, ShString format, ...);
SH_STRING_BUILDER_DEF void sh_string_builder_append_formated_valist(ShStringBuilder *builder, ShString format, va_list args);
SH_STRING_BUILDER_DEF ShString sh_string_builder_to_string(ShStringBuilder *builder, ShAllocator allocator);

// This creates a string allocated in 'allocator' from a format string and arguments.
SH_STRING_BUILDER_DEF ShString sh_string_formated(ShThreadContext *thread_context, ShAllocator allocator, ShString format, ...);

#endif // __SH_STRING_BUILDER_INCLUDE__

#ifdef SH_STRING_BUILDER_IMPLEMENTATION

SH_STRING_BUILDER_DEF void
sh_string_builder_init(ShStringBuilder *builder, ShAllocator allocator)
{
    builder->allocator = allocator;
    builder->first_buffer = NULL;
    builder->last_buffer = NULL;
}

SH_STRING_BUILDER_DEF usize
sh_string_builder_get_size(ShStringBuilder *builder)
{
    usize result = 0;

    ShStringBuffer *buffer = builder->first_buffer;

    while (buffer)
    {
        result += buffer->occupied;
        buffer = buffer->next;
    }

    return result;
}

static inline void
_sh_string_builder_expand(ShStringBuilder *builder)
{
    ShStringBuffer *new_buffer = sh_alloc_type(builder->allocator, ShStringBuffer);

    new_buffer->next = NULL;
    new_buffer->occupied = 0;

    if (builder->last_buffer)
    {
        builder->last_buffer->next = new_buffer;
    }
    else
    {
        builder->first_buffer = new_buffer;
    }

    builder->last_buffer = new_buffer;
}

SH_STRING_BUILDER_DEF void
sh_string_builder_append_u8(ShStringBuilder *builder, uint8_t c)
{
    if (!builder->last_buffer || (builder->last_buffer->occupied >= ShArrayCount(builder->last_buffer->data)))
    {
        _sh_string_builder_expand(builder);
    }

    ShStringBuffer *buffer = builder->last_buffer;
    buffer->data[buffer->occupied] = c;
    buffer->occupied += 1;
}

SH_STRING_BUILDER_DEF void
sh_string_builder_append_string(ShStringBuilder *builder, ShString str)
{
    uint8_t *src = str.data;
    usize count = str.count;

    ShStringBuffer *buffer = builder->last_buffer;

    if (buffer)
    {
        usize bytes_to_write = ShArrayCount(buffer->data) - buffer->occupied;

        if (bytes_to_write > count)
        {
            bytes_to_write = count;
        }

        count -= bytes_to_write;
        uint8_t *dst = buffer->data + buffer->occupied;
        buffer->occupied += bytes_to_write;

        while (bytes_to_write--) *dst++ = *src++;
    }

    while (count)
    {
        _sh_string_builder_expand(builder);

        ShStringBuffer *buffer = builder->last_buffer;
        usize bytes_to_write = ShArrayCount(buffer->data);

        if (bytes_to_write > count)
        {
            bytes_to_write = count;
        }

        count -= bytes_to_write;
        uint8_t *dst = buffer->data;
        buffer->occupied = bytes_to_write;

        while (bytes_to_write--) *dst++ = *src++;
    }
}

SH_STRING_BUILDER_DEF void
sh_string_builder_append_unsigned_number(ShStringBuilder *builder, uint64_t value, usize leading_character_count,
                                         uint8_t leading_character, uint64_t base, bool uppercase_digits)
{
    uint8_t buffer[64];
    usize index = ShArrayCount(buffer) - 1;

    const char *digits = "0123456789abcdefghijklmnopqrstuvwxyz";

    if (uppercase_digits)
    {
        digits = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    }

    if (!value)
    {
        buffer[index--] = '0';
    }
    else
    {
        while (value)
        {
            buffer[index--] = digits[value % base];
            value /= base;
        }
    }

    usize count = (ShArrayCount(buffer) - 1) - index;

    for (; count < leading_character_count; count++)
    {
        sh_string_builder_append_u8(builder, leading_character);
    }

    ShString str;
    str.count = ShArrayCount(buffer) - index - 1;
    str.data = buffer + index + 1;

    sh_string_builder_append_string(builder, str);
}

SH_STRING_BUILDER_DEF void
sh_string_builder_append_signed_number(ShStringBuilder *builder, int64_t value, usize leading_character_count,
                                       uint8_t leading_character, uint64_t base, bool uppercase_digits)
{
    if (value < 0)
    {
        sh_string_builder_append_u8(builder, '-');
        sh_string_builder_append_unsigned_number(builder, (uint64_t) -value, leading_character_count, leading_character, base, uppercase_digits);
    }
    else
    {
        sh_string_builder_append_unsigned_number(builder, (uint64_t) value, leading_character_count, leading_character, base, uppercase_digits);
    }
}

SH_STRING_BUILDER_DEF void
sh_string_builder_append_formated(ShStringBuilder *builder, ShString format, ...)
{
    va_list args;
    va_start(args, format);

    sh_string_builder_append_formated_valist(builder, format, args);

    va_end(args);
}

SH_STRING_BUILDER_DEF void
sh_string_builder_append_formated_valist(ShStringBuilder *builder, ShString format, va_list args)
{
    for (usize index = 0; index < format.count; index += 1)
    {
        uint8_t c = format.data[index];

        if (c == '%')
        {
            index += 1;

            if (index < format.count)
            {
                c = format.data[index];

                switch (c)
                {
                    case 'c':
                    {
                        sh_string_builder_append_u8(builder, (uint8_t) va_arg(args, int));
                    } break;

                    case 's':
                    {
                        char *c_str = va_arg(args, char *);
                        sh_string_builder_append_string(builder, ShCString(c_str));
                    } break;

                    case 'd':
                    case 'i':
                    {
                        sh_string_builder_append_signed_number(builder, va_arg(args, int), 0, '0', 10, false);
                    } break;

                    case 'x':
                    {
                        sh_string_builder_append_unsigned_number(builder, va_arg(args, unsigned int), 0, '0', 16, false);
                    } break;

                    case 'X':
                    {
                        sh_string_builder_append_unsigned_number(builder, va_arg(args, unsigned int), 0, '0', 16, true);
                    } break;

                    case 'u':
                    {
                        sh_string_builder_append_unsigned_number(builder, va_arg(args, unsigned int), 0, '0', 10, false);
                    } break;

                    case 'z':
                    {
                        index += 1;

                        if (index < format.count)
                        {
                            c = format.data[index];

                            switch (c)
                            {
                                case 'd':
                                case 'i':
                                case 'u':
                                {
                                    sh_string_builder_append_unsigned_number(builder, va_arg(args, size_t), 0, '0', 10, false);
                                } break;

                                case 'x':
                                {
                                    sh_string_builder_append_unsigned_number(builder, va_arg(args, size_t), 0, '0', 16, false);
                                } break;

                                case 'X':
                                {
                                    sh_string_builder_append_unsigned_number(builder, va_arg(args, size_t), 0, '0', 16, true);
                                } break;

                                default:
                                {
                                    sh_string_builder_append_u8(builder, '%');
                                    sh_string_builder_append_u8(builder, 'z');
                                    sh_string_builder_append_u8(builder, c);
                                } break;
                            }
                        }
                        else
                        {
                            sh_string_builder_append_u8(builder, '%');
                            sh_string_builder_append_u8(builder, 'z');
                        }
                    } break;

                    case '.':
                    {
                        index += 1;

                        if (index < format.count)
                        {
                            c = format.data[index];

                            switch (c)
                            {
                                case '*':
                                {
                                    index += 1;

                                    if (index < format.count)
                                    {
                                        c = format.data[index];

                                        switch (c)
                                        {
                                            case 's':
                                            {
                                                // TODO: what if the string is actually zero terminated, but shorter then the length provided.
                                                // this is not taken into account here.
                                                ShString str;
                                                str.count = va_arg(args, int);
                                                str.data  = (uint8_t *) va_arg(args, char *);
                                                sh_string_builder_append_string(builder, str);
                                            } break;

                                            default:
                                            {
                                                sh_string_builder_append_u8(builder, '%');
                                                sh_string_builder_append_u8(builder, '.');
                                                sh_string_builder_append_u8(builder, '*');
                                                sh_string_builder_append_u8(builder, c);
                                            } break;
                                        }
                                    }
                                    else
                                    {
                                        sh_string_builder_append_u8(builder, '%');
                                        sh_string_builder_append_u8(builder, '.');
                                        sh_string_builder_append_u8(builder, '*');
                                    }
                                } break;

                                default:
                                {
                                    sh_string_builder_append_u8(builder, '%');
                                    sh_string_builder_append_u8(builder, '.');
                                    sh_string_builder_append_u8(builder, c);
                                } break;
                            }
                        }
                        else
                        {
                            sh_string_builder_append_u8(builder, '%');
                            sh_string_builder_append_u8(builder, '.');
                        }
                    } break;

                    default:
                    {
                        sh_string_builder_append_u8(builder, '%');
                        sh_string_builder_append_u8(builder, c);
                    } break;
                }
            }
            else
            {
                sh_string_builder_append_u8(builder, '%');
            }
        }
        else
        {
            sh_string_builder_append_u8(builder, c);
        }
    }
}

SH_STRING_BUILDER_DEF ShString
sh_string_builder_to_string(ShStringBuilder *builder, ShAllocator allocator)
{
    ShString result;

    result.count = sh_string_builder_get_size(builder);
    result.data = NULL;

    if (result.count > 0)
    {
        result.data = sh_alloc_array(allocator, uint8_t, result.count);

        uint8_t *dst = result.data;

        ShStringBuffer *buffer = builder->first_buffer;

        while (buffer)
        {
            uint8_t *src = buffer->data;
            usize count = buffer->occupied;

            while (count--) *dst++ = *src++;

            buffer = buffer->next;
        }
    }

    return result;
}

SH_STRING_BUILDER_DEF ShString
sh_string_formated(ShThreadContext *thread_context, ShAllocator allocator, ShString format, ...)
{
    ShTemporaryMemory temp_memory = sh_begin_temporary_memory(thread_context, 1, &allocator);

    va_list args;
    va_start(args, format);

    ShStringBuilder builder;
    sh_string_builder_init(&builder, temp_memory.allocator);
    sh_string_builder_append_formated_valist(&builder, format, args);
    ShString result = sh_string_builder_to_string(&builder, allocator);

    va_end(args);

    sh_end_temporary_memory(temp_memory);

    return result;
}

#endif // SH_STRING_BUILDER_IMPLEMENTATION

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
