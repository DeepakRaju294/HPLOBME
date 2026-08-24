#include "replay.hpp"

#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <variant>

#include "matching_engine.hpp"

namespace lob {

namespace {

char side_to_char(Side side) {
    return side == Side::Buy ? 'B' : 'S';
}

Side char_to_side(char c) {
    if (c == 'B') {
        return Side::Buy;
    }
    if (c == 'S') {
        return Side::Sell;
    }
    throw std::runtime_error("replay: invalid side");
}

char type_to_char(OrderType type) {
    return type == OrderType::Limit ? 'L' : 'M';
}

OrderType char_to_type(char c) {
    if (c == 'L') {
        return OrderType::Limit;
    }
    if (c == 'M') {
        return OrderType::Market;
    }
    throw std::runtime_error("replay: invalid order type");
}

std::string tif_to_string(TimeInForce tif) {
    switch (tif) {
        case TimeInForce::GoodTillCancel:
            return "GTC";
        case TimeInForce::ImmediateOrCancel:
            return "IOC";
        case TimeInForce::FillOrKill:
            return "FOK";
        case TimeInForce::PostOnly:
            return "POST";
    }
    throw std::runtime_error("replay: invalid time in force");
}

TimeInForce string_to_tif(const std::string& s) {
    if (s == "GTC") {
        return TimeInForce::GoodTillCancel;
    }
    if (s == "IOC") {
        return TimeInForce::ImmediateOrCancel;
    }
    if (s == "FOK") {
        return TimeInForce::FillOrKill;
    }
    if (s == "POST") {
        return TimeInForce::PostOnly;
    }
    throw std::runtime_error("replay: invalid time in force");
}

std::vector<std::string> split(const std::string& line, char delim) {
    std::vector<std::string> fields;
    std::string field;
    std::istringstream stream(line);
    while (std::getline(stream, field, delim)) {
        fields.push_back(field);
    }
    return fields;
}

void collect_trades(const std::vector<EngineEvent>& events, std::vector<Trade>& out) {
    for (const auto& event : events) {
        if (const auto* trade_event = std::get_if<TradeExecuted>(&event)) {
            out.push_back(trade_event->trade);
        }
    }
}

} // namespace

// --- Log format (line-oriented, pipe-delimited, human-readable) ---
//   NEW|id|side(B/S)|type(L/M)|tif(GTC/IOC/FOK/POST)|price|quantity|timestamp
//   CANCEL|id|timestamp
//   REPLACE|id|new_price|new_quantity|timestamp
//   ... (repeated per command, in submission order)
//   TRADES|<count>
//   TRADE|id|aggressor_id|resting_id|side|price|quantity|timestamp|sequence
//   ... (count lines)
//   HASH|<final_state_hash>

CommandLogWriter::CommandLogWriter(const std::string& path)
    : out_(path, std::ios::out | std::ios::trunc) {
    if (!out_) {
        throw std::runtime_error("CommandLogWriter: could not open " + path);
    }
}

void CommandLogWriter::record(const NewOrder& cmd) {
    out_ << "NEW|" << cmd.id << '|' << side_to_char(cmd.side) << '|' << type_to_char(cmd.type) << '|'
         << tif_to_string(cmd.time_in_force) << '|' << cmd.price << '|' << cmd.quantity << '|' << cmd.timestamp
         << '\n';
}

void CommandLogWriter::record(const CancelOrder& cmd) {
    out_ << "CANCEL|" << cmd.id << '|' << cmd.timestamp << '\n';
}

void CommandLogWriter::record(const ReplaceOrder& cmd) {
    out_ << "REPLACE|" << cmd.id << '|' << cmd.new_price << '|' << cmd.new_quantity << '|' << cmd.timestamp << '\n';
}

void CommandLogWriter::finish(const std::vector<Trade>& trades, std::uint64_t final_state_hash) {
    out_ << "TRADES|" << trades.size() << '\n';
    for (const auto& trade : trades) {
        out_ << "TRADE|" << trade.id << '|' << trade.aggressor_order_id << '|' << trade.resting_order_id << '|'
             << side_to_char(trade.aggressor_side) << '|' << trade.price << '|' << trade.quantity << '|'
             << trade.timestamp << '|' << trade.sequence << '\n';
    }
    out_ << "HASH|" << final_state_hash << '\n';
    out_.close();
}

ReplayResult replay_from_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("replay_from_file: could not open " + path);
    }

    MatchingEngine engine;
    std::vector<Trade> replayed_trades;

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) {
            continue;
        }
        const auto fields = split(line, '|');
        const std::string& tag = fields[0];

        if (tag == "NEW") {
            NewOrder cmd{};
            cmd.id = std::stoull(fields[1]);
            cmd.side = char_to_side(fields[2][0]);
            cmd.type = char_to_type(fields[3][0]);
            cmd.time_in_force = string_to_tif(fields[4]);
            cmd.price = std::stoll(fields[5]);
            cmd.quantity = std::stoull(fields[6]);
            cmd.timestamp = std::stoull(fields[7]);
            collect_trades(engine.submit(cmd), replayed_trades);
        } else if (tag == "CANCEL") {
            CancelOrder cmd{};
            cmd.id = std::stoull(fields[1]);
            cmd.timestamp = std::stoull(fields[2]);
            engine.cancel(cmd);
        } else if (tag == "REPLACE") {
            ReplaceOrder cmd{};
            cmd.id = std::stoull(fields[1]);
            cmd.new_price = std::stoll(fields[2]);
            cmd.new_quantity = std::stoull(fields[3]);
            cmd.timestamp = std::stoull(fields[4]);
            collect_trades(engine.replace(cmd), replayed_trades);
        } else if (tag == "TRADES") {
            const std::size_t expected_count = std::stoull(fields[1]);
            std::vector<Trade> recorded_trades;
            recorded_trades.reserve(expected_count);
            for (std::size_t i = 0; i < expected_count; ++i) {
                if (!std::getline(in, line)) {
                    throw std::runtime_error("replay_from_file: truncated trade log");
                }
                const auto trade_fields = split(line, '|');
                if (trade_fields.empty() || trade_fields[0] != "TRADE") {
                    throw std::runtime_error("replay_from_file: malformed trade line");
                }
                Trade trade{};
                trade.id = std::stoull(trade_fields[1]);
                trade.aggressor_order_id = std::stoull(trade_fields[2]);
                trade.resting_order_id = std::stoull(trade_fields[3]);
                trade.aggressor_side = char_to_side(trade_fields[4][0]);
                trade.price = std::stoll(trade_fields[5]);
                trade.quantity = std::stoull(trade_fields[6]);
                trade.timestamp = std::stoull(trade_fields[7]);
                trade.sequence = std::stoull(trade_fields[8]);
                recorded_trades.push_back(trade);
            }

            if (!std::getline(in, line)) {
                throw std::runtime_error("replay_from_file: missing HASH line");
            }
            const auto hash_fields = split(line, '|');
            if (hash_fields.empty() || hash_fields[0] != "HASH") {
                throw std::runtime_error("replay_from_file: malformed hash line");
            }
            const std::uint64_t recorded_hash = std::stoull(hash_fields[1]);

            // --- Compare recorded vs. replayed outcome ---
            ReplayResult result;

            const std::size_t common = std::min(replayed_trades.size(), recorded_trades.size());
            for (std::size_t i = 0; i < common; ++i) {
                const Trade& recorded = recorded_trades[i];
                const Trade& replayed = replayed_trades[i];
                const bool same = recorded.id == replayed.id &&
                                   recorded.aggressor_order_id == replayed.aggressor_order_id &&
                                   recorded.resting_order_id == replayed.resting_order_id &&
                                   recorded.aggressor_side == replayed.aggressor_side &&
                                   recorded.price == replayed.price &&
                                   recorded.quantity == replayed.quantity &&
                                   recorded.sequence == replayed.sequence;
                if (!same) {
                    result.matched = false;
                    result.divergent_sequence = recorded.sequence;
                    result.detail = "trade mismatch at index " + std::to_string(i);
                    return result;
                }
            }

            if (replayed_trades.size() != recorded_trades.size()) {
                result.matched = false;
                const std::size_t idx = common;
                result.divergent_sequence = (idx < recorded_trades.size()) ? recorded_trades[idx].sequence
                                                                            : replayed_trades[idx].sequence;
                result.detail = "trade count mismatch: recorded " + std::to_string(recorded_trades.size()) +
                                 ", replayed " + std::to_string(replayed_trades.size());
                return result;
            }

            const std::uint64_t replayed_hash = engine.state_hash();
            if (replayed_hash != recorded_hash) {
                result.matched = false;
                result.divergent_sequence = 0;
                result.detail = "final state hash mismatch";
                return result;
            }

            result.matched = true;
            return result;
        }
    }

    throw std::runtime_error("replay_from_file: log missing TRADES/HASH footer");
}

} // namespace lob
