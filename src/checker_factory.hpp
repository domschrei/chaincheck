#pragma once

#include <memory>
#include <string>
#include "checker_interface.hpp"

enum class ProofFormat { DRAT, LRAT, SR, PALRUP, UNKNOWN };

std::unique_ptr<CheckerInterface> make_checker(
    CheckerPosition position,
    const std::string& cnf_path,
    const std::string& proof_file
);
