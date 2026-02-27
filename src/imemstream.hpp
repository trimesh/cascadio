#pragma once
#include <istream>
#include <streambuf>
#include <cstddef> // for std::size_t

/**
 * membuf: a std::streambuf over a read-only memory buffer
 */
class membuf : public std::streambuf {
public:
    membuf(const char* base, std::size_t size) {
        char* p = const_cast<char*>(base); // safe for read-only streams
        setg(p, p, p + size);
    }
};

/**
 * imemstream: std::istream wrapper over a memory buffer
 */
class imemstream : public std::istream {
public:
    imemstream(const char* base, std::size_t size)
        : std::istream(&buf_), buf_(base, size) {}

private:
    membuf buf_;
};
