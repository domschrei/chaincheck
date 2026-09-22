#pragma once

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <optional>
#include <thread>
#include <string>
#include <vector>
#include "../checker_interface.hpp"
#include "../cnf.hpp"
#include "dsr_apply.hpp"
#include "../subprocess.hpp"
#include "external_tools.hpp"

// Handles SR proofs: first dsr-trim -> LSR, then lsr-check
class SrChecker : public CheckerInterface {
    CheckerPosition _position;
    std::string _cnf_path;
    std::string _proof_file;
    std::optional<std::string> _goal_path;

    std::atomic<bool> _done{false};
    std::atomic<bool> _succeeded{false};
    std::atomic<bool> _checker_ok{false};
    std::atomic<bool> _cnf_match_ok{false};
    std::atomic<double> _cpu_seconds{0.0};
    std::atomic<double> _wall_seconds{0.0};
    std::thread _thread;

    bool expects_goal() const {
        return _position == CheckerPosition::START || _position == CheckerPosition::MIDDLE;
    }

public:
    SrChecker(CheckerPosition pos, const std::string& cnf_path, const std::string& proof)
        : _position(pos), _cnf_path(cnf_path), _proof_file(proof) {}

    void set_goal_cnf(const std::string& path) override {
        assert(expects_goal() && "set_goal_cnf is only intended for START/MIDDLE");
        _goal_path = path;
    }

    void start() override {
        // Must already be settled at this point -- START/MIDDLE need a goal,
        // END/ONLY must instead show UNSAT themselves.
        assert(_goal_path.has_value() == expects_goal());

        _thread = std::thread([this]() {
            auto wall_t0 = std::chrono::steady_clock::now();
            struct timespec t0, t1;
            clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t0);
            double child_cpu = 0.0;

            // We actually do not need to quite literally "double check" the proof at this point.
            // The proof is already being checked by dsr-trim, and the elaborated (LSR) proof can get huge.
            // As such, we disable LSR proof output and the second checking stage by setting the LSR path
            // to /dev/null here. (dsr-trim understands this and suppresses its LSR output altogether.)
            //std::string lsr_path = (std::filesystem::temp_directory_path()
            //    / (std::filesystem::path(_proof_file).filename().string() + ".lsr")).string();
            std::string lsr_path = "/dev/null";
            
            pid_t trim_pid = spawn_process({DSR_TRIM_PATH, "-q", "-f", _cnf_path, _proof_file, lsr_path});

            Cnf cnf;
            bool loaded = cnf.load(_cnf_path);
            if (!loaded)
                std::fprintf(stderr, "could not load CNF: %s\n", _cnf_path.c_str());

            // Here we use the SR proof without hints, since that matches the situation as it comes out of the solver.
            // One could also work on the dsr-trim result, but that sometimes finds a contradiction earlier.
            // Since we want to work through the chain, we do not want that earlier result but instead
            // follow the normal course, despite the extra effort.
            bool applied = loaded && apply_dsr_proof(cnf, _proof_file);
            if (loaded && !applied)
                std::fprintf(stderr, "applying the proof failed for %s\n", _proof_file.c_str());

            // Does the proof lead to the expected successor formula (START/MIDDLE) or
            // to the empty clause (END/ONLY)?
            bool result_ok = applied &&
                (_goal_path ? cnf.check_goal_is_subset(*_goal_path)
                             : cnf.has_empty_clause());
            if (applied && !result_ok && !_goal_path)
                std::fprintf(stderr, "no empty clause derived for %s (proof does not end in UNSAT)\n",
                              _proof_file.c_str());

            double trim_cpu = 0.0;
            bool trim_ok = trim_pid >= 0 && wait_for_process(trim_pid, trim_cpu);
            child_cpu += trim_cpu;
            if (!trim_ok) {
                std::fprintf(stderr, "dsr-trim failed for %s\n", _proof_file.c_str());
                clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t1);
                double thread_cpu = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
                _cpu_seconds.store(thread_cpu + child_cpu);
                _wall_seconds.store(std::chrono::duration<double>(std::chrono::steady_clock::now() - wall_t0).count());
                _checker_ok.store(false);
                _cnf_match_ok.store(result_ok);
                _succeeded.store(false);
                _done.store(true);
                if (lsr_path != "/dev/null") std::filesystem::remove(lsr_path);
                return;
            }

            // lsr-check (legitimacy of the proof steps, part 1) needs the finished
            // LSR file, so it can only start now.
            bool check_ok;
            if (lsr_path == "/dev/null") {
                check_ok = true;
            } else {
                double check_cpu = 0.0;
                pid_t check_pid = spawn_process({LSR_CHECK_PATH, "-q", _cnf_path, lsr_path});
                check_ok = (check_pid >= 0) && wait_for_process(check_pid, check_cpu);
                child_cpu += check_cpu;
                if (!check_ok)
                    std::fprintf(stderr, "lsr-check failed for %s\n", _proof_file.c_str());
                std::filesystem::remove(lsr_path);
            }

            clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t1);
            double thread_cpu = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
            _cpu_seconds.store(thread_cpu + child_cpu);
            _wall_seconds.store(std::chrono::duration<double>(std::chrono::steady_clock::now() - wall_t0).count());

            _checker_ok.store(check_ok);
            _cnf_match_ok.store(result_ok);
            _succeeded.store(check_ok && result_ok);
            _done.store(true);
        });
        _thread.detach();
    }

    bool is_done() const override { return _done.load(); }
    bool succeeded() const override { return _succeeded.load(); }
    bool checker_ok() const override { return _checker_ok.load(); }
    bool cnf_match_ok() const override { return _cnf_match_ok.load(); }
    double cpu_seconds() const override { return _cpu_seconds.load(); }
    double wall_seconds() const override { return _wall_seconds.load(); }
};
