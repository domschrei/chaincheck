#pragma once

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <optional>
#include <thread>
#include <string>
#include <unistd.h>
#include "../checker_interface.hpp"
#include "../cnf.hpp"
#include "external_tools.hpp"
#include "lsr_apply.hpp"
#include "../subprocess.hpp"

class LratChecker : public CheckerInterface {
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
    LratChecker(CheckerPosition pos, const std::string& cnf_path, const std::string& proof)
        : _position(pos), _cnf_path(cnf_path), _proof_file(proof) {}

    void set_goal_cnf(const std::string& path) override {
        assert(expects_goal() && "set_goal_cnf is only intended for START/MIDDLE");
        _goal_path = path;
    }

    void start() override {
        assert(_goal_path.has_value() == expects_goal());

        _thread = std::thread([this]() {
            auto wall_t0 = std::chrono::steady_clock::now();
            struct timespec t0, t1;
            clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t0);

            // Part 1: is the proof correct?
            int err_fd = -1;
            pid_t check_pid = spawn_process_capture_stderr({LSR_CHECK_PATH, "-q", _cnf_path, _proof_file}, err_fd);

            std::string check_stderr;
            std::thread stderr_reader;
            if (err_fd >= 0) {
                stderr_reader = std::thread([err_fd, &check_stderr]() {
                    char buf[4096];
                    ssize_t n;
                    while ((n = read(err_fd, buf, sizeof(buf))) > 0)
                        check_stderr.append(buf, (size_t)n);
                    close(err_fd);
                });
            }

            Cnf cnf;
            bool loaded = cnf.load(_cnf_path);
            if (!loaded)
                std::fprintf(stderr, "could not load CNF: %s\n", _cnf_path.c_str());
            bool applied = loaded && apply_lsr_proof(cnf, _proof_file);
            if (loaded && !applied)
                std::fprintf(stderr, "applying the proof failed for %s\n", _proof_file.c_str());

            double child_cpu = 0.0;
            bool check_ok = (check_pid >= 0) && wait_for_process(check_pid, child_cpu);
            if (stderr_reader.joinable()) stderr_reader.join();

            if (!check_ok && !check_stderr.empty()) {
                std::fprintf(stderr, "lsr-check failed for %s:\n%s",
                              _proof_file.c_str(), check_stderr.c_str());
            }

            // Part 2: does the proof lead to the expected successor formula
            // (START/MIDDLE) or to the empty clause (END/ONLY)?
            bool result_ok = applied &&
                (_goal_path ? cnf.check_goal_is_subset(*_goal_path)
                             : cnf.has_empty_clause());
            if (applied && !result_ok && !_goal_path)
                std::fprintf(stderr, "no empty clause derived for %s (proof does not end in UNSAT)\n",
                              _proof_file.c_str());

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
