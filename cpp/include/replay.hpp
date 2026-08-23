#pragma once

#include <cstdint>
#include <string>

namespace lob {

struct ReplayResult {
    bool matched{};
    // Earliest sequence number where recorded and replayed state diverged,
    // if matched is false.
    std::uint64_t divergent_sequence{};
};

// Reads a previously recorded command log, replays it through a fresh
// MatchingEngine, and compares the resulting trades, event ordering, final
// book, and state hash against what was recorded (see spec section 15).
//
// Parsing time must not be included in matching-engine benchmarks.
//
// TODO(Milestone 3): command log format, checkpoint hashes, divergence
// reporting.
ReplayResult replay_from_file(const std::string& path);

} // namespace lob
