#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// Brings a clause into canonical form: literals sorted by variable, duplicate
// literals removed. Returns true if the clause is thereby recognised as
// tautological (containing both x and -x) -- a tautological clause is always
// satisfied and is ignored when comparing.
inline bool canonicalize_clause(std::vector<int>& clause) {
    std::sort(clause.begin(), clause.end(), [](int a, int b) {
        int va = std::abs(a), vb = std::abs(b);
        return va != vb ? va < vb : a < b;
    });
    clause.erase(std::unique(clause.begin(), clause.end()), clause.end());

    for (size_t i = 1; i < clause.size(); i++)
        if (clause[i] == -clause[i - 1]) return true;
    return false;
}


class Cnf {
public:
    // Loads a DIMACS CNF file; clauses get IDs 1..n in file order.
    bool load(const std::string& path) {
        FILE* f = fopen(path.c_str(), "r");
        if (!f) return false;

        clauses_.clear();
        derived_empty_clause_ = false;
        declared_num_vars_ = 0;
        declared_num_clauses_ = 0;

        int next_id = 1;
        std::vector<int> clause;
        auto handle_lit = [&](int lit) {
            if (lit == 0) {
                set_clause(next_id++, std::move(clause));
                clause.clear();
            } else {
                clause.push_back(lit);
            }
        };

        int c, sign = 1, num = 0;
        bool began = false, in_comment = false;
        while ((c = fgetc(f)) != EOF) {
            if (in_comment) {
                if (c == '\n') in_comment = false;
                continue;
            }
            switch (c) {
            case 'c':
                in_comment = true;
                break;
            case 'p': {
                char line[256];
                int len = 0, ch;
                while ((ch = fgetc(f)) != EOF && ch != '\n' && len < (int)sizeof(line) - 1)
                    line[len++] = (char)ch;
                line[len] = '\0';
                int nv = 0, nc = 0;
                if (std::sscanf(line, " cnf %d %d", &nv, &nc) == 2) {
                    declared_num_vars_ = nv;
                    declared_num_clauses_ = nc;
                    clauses_.reserve(nc);
                }
                break;
            }
            case '-':
                sign = -1;
                began = true;
                break;
            case '0': case '1': case '2': case '3': case '4':
            case '5': case '6': case '7': case '8': case '9':
                num = num * 10 + (c - '0');
                began = true;
                break;
            case ' ': case '\t': case '\n': case '\r':
                if (began) {
                    handle_lit(sign * num);
                    sign = 1; num = 0; began = false;
                }
                break;
            }
        }
        if (began) handle_lit(sign * num);

        fclose(f);
        return true;
    }

    void add_clause(int id, std::vector<int> clause) {
        set_clause(id, std::move(clause));
    }

    void remove_clause(int id) {
        if (id >= 1 && id <= (int)clauses_.size())
            clauses_[id - 1] = std::nullopt;
    }

    void remove_clause_by_literals(std::vector<int> lits) {
        if (canonicalize_clause(lits)) return;

        build_content_index();
        auto range = content_index_.equal_range(clause_hash(lits));
        for (auto it = range.first; it != range.second; ++it) {
            auto& slot = clauses_[it->second - 1];
            if (slot && *slot == lits) {
                slot = std::nullopt;
                content_index_.erase(it);
                return;
            }
        }
    }


    int next_free_id() const { return (int)clauses_.size() + 1; }


    bool check_goal_is_subset(const std::string& path) const {
        Cnf goal;
        if (!goal.load(path)) return false;
        auto own = canonical_clause_set();
        auto goal_clauses = goal.canonical_clause_set();

        std::vector<std::vector<int>> missing;
        std::set_difference(goal_clauses.begin(), goal_clauses.end(),
                             own.begin(), own.end(), std::back_inserter(missing));

        int nbEnumerated = 0;
        for (const auto& clause : missing) {
            std::fprintf(stderr, "clause (");
            for (size_t i = 0; i < clause.size(); i++)
                std::fprintf(stderr, "%s%d", i ? " " : "", clause[i]);
            std::fprintf(stderr, ") occurs only in the second CNF (%s), not in the first\n",
                         path.c_str());
            nbEnumerated++;
            if (nbEnumerated == 10 && missing.size() > 10) {
                std::fprintf(stderr, "... as well as %lu further clauses\n",
                    missing.size()-nbEnumerated);
                break;
            }
        }

        return missing.empty();
    }

    // true as soon as an empty clause has been added at some point
    bool has_empty_clause() const { return derived_empty_clause_; }

    int declared_num_vars() const { return declared_num_vars_; }
    int declared_num_clauses() const { return declared_num_clauses_; }

private:
    void set_clause(int id, std::vector<int> clause) {
        if (clause.empty()) derived_empty_clause_ = true;
        
        canonicalize_clause(clause);
        if (id > (int)clauses_.size()) {
            if (id > (int)clauses_.capacity())
                clauses_.reserve(std::max<size_t>(id, clauses_.capacity() * 2));
            clauses_.resize(id);
        }
        clauses_[id - 1] = std::move(clause);
        if (content_index_built_)
            content_index_.emplace(clause_hash(*clauses_[id - 1]), id);
    }

    static size_t clause_hash(const std::vector<int>& clause) {
        size_t h = 1469598103934665603ull;
        for (int lit : clause) {
            h ^= (size_t)(unsigned)lit;
            h *= 1099511628211ull;
        }
        return h;
    }

    // The index costs memory proportional to the formula size and is only needed
    // by the DSR path -- so build it only on the first content-based deletion.
    // The LRAT/LSR path deletes by ID and pays nothing for it.
    void build_content_index() {
        if (content_index_built_) return;
        content_index_built_ = true;
        content_index_.reserve(clauses_.size());
        for (size_t i = 0; i < clauses_.size(); i++)
            if (clauses_[i]) content_index_.emplace(clause_hash(*clauses_[i]), (int)(i + 1));
    }

    std::vector<std::vector<int>> canonical_clause_set() const {
        std::vector<std::vector<int>> result;
        result.reserve(clauses_.size());
        for (const auto& c : clauses_) {
            if (!c) continue;
            std::vector<int> clause = *c;
            if (!canonicalize_clause(clause))
                result.push_back(std::move(clause));
        }
        std::sort(result.begin(), result.end());
        result.erase(std::unique(result.begin(), result.end()), result.end());
        return result;
    }

    std::vector<std::optional<std::vector<int>>> clauses_;
    // Hash of the canonical clause -> ID. Clauses occurring more than once are
    // stored as several entries, so a deletion hits exactly one occurrence.
    std::unordered_multimap<size_t, int> content_index_;
    bool content_index_built_ = false;
    bool derived_empty_clause_ = false;
    int declared_num_vars_ = 0;
    int declared_num_clauses_ = 0;
};
