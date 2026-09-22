#include "checker_factory.hpp"
#include "checker/lrat_checker.hpp"
#include "checker/sr_checker.hpp"
#include "checker/palrup_checker.hpp"

#include <filesystem>
#include <iostream>

static ProofFormat detect_format(const std::string& proof_file) {
    std::string ext = std::filesystem::path(proof_file).extension().string();
    if (ext == ".lrat")   return ProofFormat::LRAT;
    if (ext == ".drat")   return ProofFormat::DRAT;
    if (ext == ".sr")     return ProofFormat::SR;
    if (ext == ".palrup") return ProofFormat::PALRUP;
    return ProofFormat::UNKNOWN;
}

std::unique_ptr<CheckerInterface> make_checker(
    CheckerPosition position,
    const std::string& cnf_path,
    const std::string& proof_file)
{
    ProofFormat format = detect_format(proof_file);
    switch (format) {
        case ProofFormat::LRAT:
            return std::make_unique<LratChecker>(position, cnf_path, proof_file);
        case ProofFormat::DRAT:
        case ProofFormat::SR:
            return std::make_unique<SrChecker>(position, cnf_path, proof_file);
        case ProofFormat::PALRUP:
            return std::make_unique<PalRupChecker>(position, cnf_path, proof_file);
        case ProofFormat::UNKNOWN:
            std::cerr << "Error: unknown proof format: "
                      << std::filesystem::path(proof_file).filename() << "\n";
            return nullptr;
    }
    return nullptr;
}
