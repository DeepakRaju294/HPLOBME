#pragma once

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "order.hpp"
#include "trade.hpp"

namespace lob {

// Appends NewOrder/CancelOrder/ReplaceOrder commands to a deterministic,
// human-readable log, then the recorded run's outcome (its trades, in
// order, plus its final state_hash()) so a later replay has something to
// verify against (spec section 15). See replay.cpp for the exact line
// format. This is a correctness/test tool, not part of the matching-engine
// hot path -- never use it inside a timed benchmark section.
class CommandLogWriter {
public:
    explicit CommandLogWriter(const std::string& path);

    void record(const NewOrder& cmd);
    void record(const CancelOrder& cmd);
    void record(const ReplaceOrder& cmd);

    // Appends the recorded run's outcome. Call exactly once, after the
    // last command, before the file is used for replay.
    void finish(const std::vector<Trade>& trades, std::uint64_t final_state_hash);

private:
    std::ofstream out_;
};

struct ReplayResult {
    bool matched{};
    // First point of divergence, if matched is false: the sequence number
    // of the first mismatched trade, or 0 if trade counts/values all
    // matched but the final state hash still differed.
    SequenceNumber divergent_sequence{};
    std::string detail;
};

// Reads a log written by CommandLogWriter, replays every command through
// a fresh MatchingEngine, and compares the resulting trades (identical
// values, identical order) and final state_hash() against what was
// recorded. Replay parsing time is not meant to be included in
// matching-engine benchmarks (spec section 15) -- this function only
// exists for correctness verification.
ReplayResult replay_from_file(const std::string& path);

} // namespace lob
