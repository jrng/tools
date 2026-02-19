// sh_platform.h - MIT License
// See end of file for full license

#ifndef __SH_PLATFORM_INCLUDE__
#define __SH_PLATFORM_INCLUDE__

#  ifndef __SH_BASE_INCLUDE__
#    error "sh_platform.h requires sh_base.h to be included first"
#  endif

#  ifndef __SH_STRING_BUILDER_INCLUDE__
#    error "sh_platform.h requires sh_string_builder.h to be included first"
#  endif

#  if SH_PLATFORM_WINDOWS

#    define NOMINMAX
#    define WIN32_LEAN_AND_MEAN

#    include <windows.h>

#  elif SH_PLATFORM_UNIX

#    include <fcntl.h>
#    include <unistd.h>
#    include <sys/stat.h>

#  endif

#  if defined(SH_STATIC) || defined(SH_PLATFORM_STATIC)
#    define SH_PLATFORM_DEF static
#  else
#    define SH_PLATFORM_DEF extern
#  endif

SH_PLATFORM_DEF bool sh_read_entire_file(ShThreadContext *thread_context, ShAllocator allocator, ShString filename, ShString *content);
SH_PLATFORM_DEF bool sh_write_entire_file(ShThreadContext *thread_context, ShString filename, ShStringBuilder *content);

#endif // __SH_PLATFORM_INCLUDE__

#ifdef SH_PLATFORM_IMPLEMENTATION

SH_PLATFORM_DEF bool
sh_read_entire_file(ShThreadContext *thread_context, ShAllocator allocator, ShString filename, ShString *content)
{
#  if SH_PLATFORM_WINDOWS
    ShTemporaryMemory temp_memory = sh_begin_temporary_memory(thread_context, 1, &allocator);

    LPWSTR utf16_filename = (LPWSTR) sh_string_to_c_string(temp_memory.allocator, sh_string_utf8_to_utf16le(temp_memory.allocator, filename));
    HANDLE file = CreateFileW(utf16_filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, 0);

    if (file == INVALID_HANDLE_VALUE)
    {
        sh_end_temporary_memory(temp_memory);
        return false;
    }

    LARGE_INTEGER file_size;

    if (!GetFileSizeEx(file, &file_size))
    {
        sh_end_temporary_memory(temp_memory);
        CloseHandle(file);
        return false;
    }

    content->count = file_size.QuadPart;
    content->data  = sh_alloc_array(allocator, uint8_t, content->count);

    usize index = 0;

    while (index < content->count)
    {
        DWORD bytes_read = 0;

        if (!ReadFile(file, content->data + index, content->count - index, &bytes_read, NULL))
        {
            sh_free(allocator, content->data);
            sh_end_temporary_memory(temp_memory);
            CloseHandle(file);
            return false;
        }

        index += bytes_read;
    }

    sh_end_temporary_memory(temp_memory);
    CloseHandle(file);

    return true;
#  elif SH_PLATFORM_UNIX
    ShTemporaryMemory temp_memory = sh_begin_temporary_memory(thread_context, 1, &allocator);

    int fd = open(sh_string_to_c_string(temp_memory.allocator, filename), O_RDONLY);

    if (fd < 0)
    {
        sh_end_temporary_memory(temp_memory);
        return false;
    }

    struct stat stats;

    if (fstat(fd, &stats) < 0)
    {
        sh_end_temporary_memory(temp_memory);
        close(fd);
        return false;
    }

    content->count = stats.st_size;
    content->data  = sh_alloc_array(allocator, uint8_t, content->count);

    usize index = 0;

    while (index < content->count)
    {
        ssize_t read_bytes = read(fd, content->data + index, content->count - index);

        if (read_bytes < 0)
        {
            sh_free(allocator, content->data);
            sh_end_temporary_memory(temp_memory);
            close(fd);
            return false;
        }

        index += read_bytes;
    }

    sh_end_temporary_memory(temp_memory);
    close(fd);

    return true;
#  else
    return false;
#  endif
}

SH_PLATFORM_DEF bool
sh_write_entire_file(ShThreadContext *thread_context, ShString filename, ShStringBuilder *content)
{
#  if SH_PLATFORM_WINDOWS
    ShTemporaryMemory temp_memory = sh_begin_temporary_memory(thread_context, 0, NULL);

    LPWSTR utf16_filename = (LPWSTR) sh_string_to_c_string(temp_memory.allocator, sh_string_utf8_to_utf16le(temp_memory.allocator, filename));
    HANDLE file = CreateFileW(utf16_filename, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);

    sh_end_temporary_memory(temp_memory);

    if (file == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    ShStringBuffer *buffer = content->first_buffer;

    while (buffer)
    {
        uint8_t *src = buffer->data;
        usize count = buffer->occupied;

        while (count)
        {
            DWORD bytes_written = 0;

            if (!WriteFile(file, src, count, &bytes_written, 0))
            {
                CloseHandle(file);
                return false;
            }

            src += bytes_written;
            count -= bytes_written;
        }

        buffer = buffer->next;
    }

    CloseHandle(file);

    return true;
#  elif SH_PLATFORM_UNIX
    ShTemporaryMemory temp_memory = sh_begin_temporary_memory(thread_context, 0, NULL);

    int fd = open(sh_string_to_c_string(temp_memory.allocator, filename),  O_WRONLY | O_TRUNC | O_CREAT, 0664);

    sh_end_temporary_memory(temp_memory);

    if (fd < 0)
    {
        return false;
    }

    ShStringBuffer *buffer = content->first_buffer;

    while (buffer)
    {
        uint8_t *src = buffer->data;
        usize count = buffer->occupied;

        while (count)
        {
            ssize bytes_written = write(fd, src, count);

            if (bytes_written < 0)
            {
                close(fd);
                return false;
            }

            src += bytes_written;
            count -= bytes_written;
        }

        buffer = buffer->next;
    }

    close(fd);

    return true;
#  else
    return false;
#  endif
}

#endif // SH_PLATFORM_IMPLEMENTATION

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
