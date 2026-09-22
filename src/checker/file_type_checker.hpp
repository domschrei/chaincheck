
#pragma once

#include <fstream>
#include <vector>
#include <iostream>
#include <string>

struct FileTypeChecker {

    static constexpr std::size_t SAMPLE_SIZE = 8192;

    static bool is_text_byte(unsigned char c) {
        if (c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v')
            return true;
        if (c >= 0x20 && c < 0x7F)
            return true;
        if (c >= 0x80)
            return true;
        return false;
    }

    // Returns true if file looks binary.
    static bool is_file_binary(const std::string& path) {
        std::ifstream f(path, std::ios::binary);
        if (!f) {
            throw std::runtime_error("could not open file: " + path);
        }

        std::vector<unsigned char> buf(SAMPLE_SIZE);
        f.read(reinterpret_cast<char*>(buf.data()), buf.size());
        std::streamsize n = f.gcount();

        if (n == 0) return false; // empty file -> text

        std::size_t suspicious = 0;
        for (std::streamsize i = 0; i < n; i++) {
            unsigned char c = buf[static_cast<size_t>(i)];
            if (c == 0x00) {
                return true; // NUL byte -> binary
            }
            if (!is_text_byte(c)) {
                suspicious++;
            }
        }

        double ratio = static_cast<double>(suspicious) / static_cast<double>(n);
        // could also be more lenient, but that doesn't seem to make sense for proofs
        return ratio > 0;
    }
};
