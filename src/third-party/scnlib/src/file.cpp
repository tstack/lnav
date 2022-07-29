// Copyright 2017 Elias Kosunen
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// This file is a part of scnlib:
//     https://github.com/eliaskosunen/scnlib

#if defined(SCN_HEADER_ONLY) && SCN_HEADER_ONLY
#define SCN_FILE_CPP
#endif

#include <scn/detail/error.h>
#include <scn/detail/file.h>
#include <scn/util/expected.h>

#include <cstdio>

#if SCN_POSIX
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#elif SCN_WINDOWS

#ifdef WIN32_LEAN_AND_MEAN
#define SCN_WIN32_LEAN_DEFINED 1
#else
#define WIN32_LEAN_AND_MEAN
#define SCN_WIN32_LEAN_DEFINED 0
#endif

#ifdef NOMINMAX
#define SCN_NOMINMAX_DEFINED 1
#else
#define NOMINMAX
#define SCN_NOMINMAX_DEFINED 0
#endif

#include <Windows.h>

#if !SCN_NOMINMAX_DEFINED
#undef NOMINMAX
#endif

#if !SCN_WIN32_LEAN_DEFINED
#undef WIN32_LEAN_AND_MEAN
#endif

#endif

namespace scn {
    SCN_BEGIN_NAMESPACE

    namespace detail {
        SCN_FUNC native_file_handle native_file_handle::invalid()
        {
#if SCN_WINDOWS
            return {INVALID_HANDLE_VALUE};
#else
            return {-1};
#endif
        }

        SCN_FUNC byte_mapped_file::byte_mapped_file(const char* filename)
        {
#if SCN_POSIX
            int fd = open(filename, O_RDONLY);
            if (fd == -1) {
                return;
            }

            struct stat s {
            };
            int status = fstat(fd, &s);
            if (status == -1) {
                close(fd);
                return;
            }
            auto size = s.st_size;

            auto ptr =
                static_cast<char*>(mmap(nullptr, static_cast<size_t>(size),
                                        PROT_READ, MAP_PRIVATE, fd, 0));
            if (ptr == MAP_FAILED) {
                close(fd);
                return;
            }

            m_file.handle = fd;
            m_map = span<char>{ptr, static_cast<size_t>(size)};
#elif SCN_WINDOWS
            auto f = ::CreateFileA(
                filename, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (f == INVALID_HANDLE_VALUE) {
                return;
            }

            LARGE_INTEGER _size;
            if (::GetFileSizeEx(f, &_size) == 0) {
                ::CloseHandle(f);
                return;
            }
            auto size = static_cast<size_t>(_size.QuadPart);

            auto h = ::CreateFileMappingA(
                f, nullptr, PAGE_READONLY,
#ifdef _WIN64
                static_cast<DWORD>(size >> 32ull),
#else
                DWORD{0},
#endif
                static_cast<DWORD>(size & 0xffffffffull), nullptr);
            if (h == INVALID_HANDLE_VALUE || h == nullptr) {
                ::CloseHandle(f);
                return;
            }

            auto start = ::MapViewOfFile(h, FILE_MAP_READ, 0, 0, size);
            if (!start) {
                ::CloseHandle(h);
                ::CloseHandle(f);
                return;
            }

            m_file.handle = f;
            m_map_handle.handle = h;
            m_map = span<char>{static_cast<char*>(start), size};
#else
            SCN_UNUSED(filename);
#endif
        }

        SCN_FUNC void byte_mapped_file::_destruct()
        {
#if SCN_POSIX
            munmap(m_map.data(), m_map.size());
            close(m_file.handle);
#elif SCN_WINDOWS
            ::CloseHandle(m_map_handle.handle);
            ::CloseHandle(m_file.handle);
            m_map_handle = native_file_handle::invalid();
#endif

            m_file = native_file_handle::invalid();
            m_map = span<char>{};

            SCN_ENSURE(!valid());
        }

    }  // namespace detail

    namespace detail {
        template <typename CharT>
        struct basic_file_iterator_access {
            using iterator = typename basic_file<CharT>::iterator;

            basic_file_iterator_access(const iterator& it) : self(it) {}

            SCN_NODISCARD expected<CharT> deref() const
            {
                SCN_EXPECT(self.m_file);

                if (self.m_file->m_buffer.empty()) {
                    // no chars have been read
                    return self.m_file->_read_single();
                }
                if (!self.m_last_error) {
                    // last read failed
                    return self.m_last_error;
                }
                return self.m_file->_get_char_at(self.m_current);
            }

            SCN_NODISCARD bool eq(const iterator& o) const
            {
                if (self.m_file && (self.m_file == o.m_file || !o.m_file)) {
                    if (self.m_file->_is_at_end(self.m_current) &&
                        self.m_last_error.code() != error::end_of_range &&
                        !o.m_file) {
                        self.m_last_error = error{};
                        auto r = self.m_file->_read_single();
                        if (!r) {
                            self.m_last_error = r.error();
                            return !o.m_file || self.m_current == o.m_current ||
                                   o.m_last_error.code() == error::end_of_range;
                        }
                    }
                }

                // null file == null file
                if (!self.m_file && !o.m_file) {
                    return true;
                }
                // null file == eof file
                if (!self.m_file && o.m_file) {
                    // lhs null, rhs potentially eof
                    return o.m_last_error.code() == error::end_of_range;
                }
                // eof file == null file
                if (self.m_file && !o.m_file) {
                    // rhs null, lhs potentially eof
                    return self.m_last_error.code() == error::end_of_range;
                }
                // eof file == eof file
                if (self.m_last_error == o.m_last_error &&
                    self.m_last_error.code() == error::end_of_range) {
                    return true;
                }

                return self.m_file == o.m_file && self.m_current == o.m_current;
            }

            const iterator& self;
        };
    }  // namespace detail

    template <>
    SCN_FUNC expected<char> basic_file<char>::iterator::operator*() const
    {
        return detail::basic_file_iterator_access<char>(*this).deref();
    }
    template <>
    SCN_FUNC expected<wchar_t> basic_file<wchar_t>::iterator::operator*() const
    {
        return detail::basic_file_iterator_access<wchar_t>(*this).deref();
    }

    template <>
    SCN_FUNC bool basic_file<char>::iterator::operator==(
        const basic_file<char>::iterator& o) const
    {
        return detail::basic_file_iterator_access<char>(*this).eq(o);
    }
    template <>
    SCN_FUNC bool basic_file<wchar_t>::iterator::operator==(
        const basic_file<wchar_t>::iterator& o) const
    {
        return detail::basic_file_iterator_access<wchar_t>(*this).eq(o);
    }

    template <>
    SCN_FUNC expected<char> file::_read_single() const
    {
        SCN_EXPECT(valid());
        int tmp = std::fgetc(m_file);
        if (tmp == EOF) {
            if (std::feof(m_file) != 0) {
                return error(error::end_of_range, "EOF");
            }
            if (std::ferror(m_file) != 0) {
                return error(error::source_error, "fgetc error");
            }
            return error(error::unrecoverable_source_error,
                         "Unknown fgetc error");
        }
        auto ch = static_cast<char>(tmp);
        m_buffer.push_back(ch);
        return ch;
    }
    template <>
    SCN_FUNC expected<wchar_t> wfile::_read_single() const
    {
        SCN_EXPECT(valid());
        wint_t tmp = std::fgetwc(m_file);
        if (tmp == WEOF) {
            if (std::feof(m_file) != 0) {
                return error(error::end_of_range, "EOF");
            }
            if (std::ferror(m_file) != 0) {
                return error(error::source_error, "fgetc error");
            }
            return error(error::unrecoverable_source_error,
                         "Unknown fgetc error");
        }
        auto ch = static_cast<wchar_t>(tmp);
        m_buffer.push_back(ch);
        return ch;
    }

    template <>
    SCN_FUNC void file::_sync_until(std::size_t pos) noexcept
    {
        for (auto it = m_buffer.rbegin();
             it != m_buffer.rend() - static_cast<std::ptrdiff_t>(pos); ++it) {
            std::ungetc(static_cast<unsigned char>(*it), m_file);
        }
    }
    template <>
    SCN_FUNC void wfile::_sync_until(std::size_t pos) noexcept
    {
        for (auto it = m_buffer.rbegin();
             it != m_buffer.rend() - static_cast<std::ptrdiff_t>(pos); ++it) {
            std::ungetwc(static_cast<wint_t>(*it), m_file);
        }
    }

    SCN_END_NAMESPACE
}  // namespace scn
