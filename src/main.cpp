#include <chrono>
#include <iostream>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <vector>
#include <map>
#include <string>
#include <thread>
#include <regex>

#include "checker_factory.hpp"

namespace fs = std::filesystem;

static const std::array<std::string, 4> allowed_extensions = {".drat", ".lrat", ".sr", ".palrup"};

static int parse_step_number(const fs::path& path) {
    std::string stem = path.stem().string();
    std::string ext  = path.extension().string();
    std::smatch m;

    if (std::regex_match(stem, m, std::regex(R"(post(\d+))")) && ext == ".cnf")
        return std::stoi(m[1].str());

    if (std::regex_match(stem, m, std::regex(R"(step(\d+))"))) {
        for (const auto& e : allowed_extensions)
            if (ext == e) return std::stoi(m[1].str());
    }

    return -1;
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <formula.cnf> <proof-dir>\n";
        return 1;
    }

    fs::path formulaPath(argv[1]);
    fs::path proofDir(argv[2]);

    if (!fs::exists(formulaPath)) {
        std::cerr << "Error: formula file not found: " << formulaPath << "\n";
        return 1;
    }
    if (!fs::is_directory(proofDir)) {
        std::cerr << "Error: proof directory not found: " << proofDir << "\n";
        return 1;
    }

    // Split the files: 's' files (step) and 'p' files (post)
    std::map<int, fs::path> stepMap;
    std::map<int, fs::path> postMap;

    for (const auto& entry : fs::directory_iterator(proofDir)) {
        const fs::path& p = entry.path();
        std::string stem = p.stem().string();
        if (stem.empty()) continue;
        int n = parse_step_number(p);
        if (n < 0) {
            std::cerr << "Error: unexpected file in proof directory: "
                      << p.filename() << "\n"
                      << "  Expected: step<N>.<lrat|sr|palrup> or post<N>.cnf\n";
            return 1;
        }

        if (stem.find("step") == 0)
            stepMap[n] = p;
        else if (stem.find("post") == 0)
            postMap[n] = p;
    }

    if (stepMap.empty()) {
        std::cerr << "Error: no step files found in " << proofDir << "\n";
        return 1;
    }

    int total = (int)stepMap.size();

    // post count must be exactly 1 less than step count (no post after the last step)
    if ((int)postMap.size() != total - 1) {
        std::cerr << "Error: expected " << total - 1 << " post files, found "
                  << postMap.size() << "\n";
        return 1;
    }

    std::cout << "Formula:   " << fs::absolute(formulaPath) << "\n";
    std::cout << "Proof dir: " << fs::absolute(proofDir) << "\n";
    std::cout << "Steps:     " << total << "\n\n";

    std::vector<std::unique_ptr<CheckerInterface>> checkers;

    if (total == 1) {
        checkers.push_back(make_checker(CheckerPosition::ONLY, formulaPath.string(), stepMap[1].string()));
    } else {
        checkers.push_back(make_checker(CheckerPosition::START, formulaPath.string(), stepMap[1].string()));
        for (int i = 2; i < total; i++)
            checkers.push_back(make_checker(CheckerPosition::MIDDLE, postMap[i-1].string(), stepMap[i].string()));
        checkers.push_back(make_checker(CheckerPosition::END, postMap[total-1].string(), stepMap[total].string()));

        for (int i = 0; i < total - 1; i++)
            checkers[i]->set_goal_cnf(postMap[i + 1].string());
    }

    for (auto& c : checkers) c->start();

    bool any_pending = true;
    while (any_pending) {
        any_pending = false;
        for (auto& c : checkers) if (!c->is_done()) { any_pending = true; break; }
        if (any_pending) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    std::cout << "\n"
              << std::left << std::setw(6) << "Step"
              << std::setw(20) << "File"
              << std::setw(8) << "Result"
              << std::setw(10) << "Checker"
              << std::setw(10) << "CnfMatch"
              << std::right << std::setw(12) << "Wall(s)"
              << std::setw(12) << "CPU(s)" << "\n";

    bool all_ok = true;
    for (size_t i = 0; i < checkers.size(); i++) {
        bool ok = checkers[i]->succeeded();
        all_ok = all_ok && ok;
        std::string cnf_match_str = checkers[i]->has_cnf_match()
            ? (checkers[i]->cnf_match_ok() ? "OK" : "FAILED")
            : "n/a";
        std::cout << std::left << std::setw(6) << (i + 1)
                  << std::setw(20) << stepMap[(int)i + 1].filename().string()
                  << std::setw(8) << (ok ? "OK" : "FAILED")
                  << std::setw(10) << (checkers[i]->checker_ok() ? "OK" : "FAILED")
                  << std::setw(10) << cnf_match_str
                  << std::right << std::fixed << std::setprecision(3)
                  << std::setw(12) << checkers[i]->wall_seconds()
                  << std::setw(12) << checkers[i]->cpu_seconds() << "\n";
    }

    if (!all_ok) {
        std::cerr << "\nError: proof verification failed\n";
        return 1;
    }

    std::cout << "\nAll steps verified.\n";
    return 0;
}
