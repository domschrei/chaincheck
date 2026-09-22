#pragma once

#include <cstdio>
#include <string>
#include <vector>

#include "../cnf.hpp"
#include "parse_util.hpp"
#include "file_type_checker.hpp"

inline bool apply_lsr_proof(Cnf& cnf, const std::string& lsr_path) {

    FILE* f;
    bool compressed = FileTypeChecker::is_file_binary(lsr_path);
    if (compressed) {
        printf("LSR: Seems to be binary file, using decompression\n");
        std::string cmd = std::string(DSR_COMPRESS_PATH) + " -d " + lsr_path;
        f = popen(cmd.c_str(), "r");
    } else {
        printf("LSR: Seems to be plaintext file, reading directly\n");
        f = fopen(lsr_path.c_str(), "r");
    }
    if (!f) return false;

    int id;
    while (read_int(f, id)) {
        if (peek_after_whitespace(f) == 'd') {
            fgetc(f);  // consume the 'd'
            int del_id;
            while (read_int(f, del_id) && del_id != 0)
                cnf.remove_clause(del_id);
            continue;
        }

        
        std::vector<int> clause;
        int pivot = 0;
        bool have_pivot = false;
        int lit;
        while (read_int(f, lit) && lit != 0) {
            if (!have_pivot) {
                pivot = lit;
                have_pivot = true;
                clause.push_back(lit);
            } else if (lit == pivot) {
                while (read_int(f, lit) && lit != 0) {}  // skip the witness
                break;
            } else {
                clause.push_back(lit);
            }
        }

        int hint;
        while (read_int(f, hint) && hint != 0) {}  // skip the hints

        cnf.add_clause(id, std::move(clause));
    }

    if (compressed) {
        int status = pclose(f);
        if (status == -1) {
            perror("pclose failed");
        } else if (status != 0) {
            printf("LSR decompression exited with status %d\n", status);
        }
    } else {
        fclose(f);
    }
    return true;
}
