#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <variant>

#include "matching_engine.hpp"
#include "replay.hpp"

using namespace lob;

namespace {

NewOrder make_new_order(OrderId id, Side side, Price price, Quantity qty,
                         TimeInForce tif = TimeInForce::GoodTillCancel,
                         OrderType type = OrderType::Limit,
                         Timestamp ts = 0) {
    NewOrder cmd{};
    cmd.id = id;
    cmd.side = side;
    cmd.type = type;
    cmd.time_in_force = tif;
    cmd.price = price;
    cmd.quantity = qty;
    cmd.timestamp = ts;
    return cmd;
}

std::string temp_log_path(const std::string& name) {
    auto path = std::filesystem::temp_directory_path() / ("lob_replay_test_" + name + ".log");
    return path.string();
}

void collect_trades(const std::vector<EngineEvent>& events, std::vector<Trade>& out) {
    for (const auto& event : events) {
        if (const auto* trade_event = std::get_if<TradeExecuted>(&event)) {
            out.push_back(trade_event->trade);
        }
    }
}

// Runs a representative command sequence (adds, cancels, replaces, and
// crosses that produce trades) through `engine`, recording each command
// to `writer` as it goes, and returns all trades produced.
std::vector<Trade> run_reference_sequence(MatchingEngine& engine, CommandLogWriter& writer) {
    std::vector<Trade> trades;

    auto submit_and_record = [&](const NewOrder& cmd) {
        writer.record(cmd);
        collect_trades(engine.submit(cmd), trades);
    };

    submit_and_record(make_new_order(1, Side::Sell, 50, 10, TimeInForce::GoodTillCancel, OrderType::Limit, 1));
    submit_and_record(make_new_order(2, Side::Sell, 51, 5, TimeInForce::GoodTillCancel, OrderType::Limit, 2));
    submit_and_record(make_new_order(3, Side::Buy, 49, 8, TimeInForce::GoodTillCancel, OrderType::Limit, 3));

    CancelOrder cancel_cmd{};
    cancel_cmd.id = 3;
    cancel_cmd.timestamp = 4;
    writer.record(cancel_cmd);
    engine.cancel(cancel_cmd);

    ReplaceOrder replace_cmd{};
    replace_cmd.id = 2;
    replace_cmd.new_price = 51;
    replace_cmd.new_quantity = 3; // priority-preserving reduction
    replace_cmd.timestamp = 5;
    writer.record(replace_cmd);
    engine.replace(replace_cmd);

    // Aggressive buy sweeping both remaining sell levels.
    submit_and_record(make_new_order(4, Side::Buy, 100, 20, TimeInForce::ImmediateOrCancel, OrderType::Limit, 6));

    return trades;
}

} // namespace

TEST(Replay, ReproducesIdenticalTradesAndFinalState) {
    const std::string path = temp_log_path("basic");

    MatchingEngine recorded_engine;
    CommandLogWriter writer(path);
    auto recorded_trades = run_reference_sequence(recorded_engine, writer);
    writer.finish(recorded_trades, recorded_engine.state_hash());

    ReplayResult result = replay_from_file(path);

    EXPECT_TRUE(result.matched) << result.detail;
    EXPECT_TRUE(result.detail.empty());

    std::filesystem::remove(path);
}

TEST(Replay, DetectsTradeDivergenceFromCorruptedLog) {
    const std::string path = temp_log_path("corrupt_trade");

    MatchingEngine recorded_engine;
    CommandLogWriter writer(path);
    auto recorded_trades = run_reference_sequence(recorded_engine, writer);
    writer.finish(recorded_trades, recorded_engine.state_hash());

    // Corrupt the first recorded trade's quantity field so it no longer
    // matches what replay will actually produce.
    {
        std::ifstream in(path);
        std::vector<std::string> lines;
        std::string line;
        while (std::getline(in, line)) {
            lines.push_back(line);
        }
        in.close();

        std::ofstream out(path, std::ios::trunc);
        bool corrupted = false;
        for (auto& l : lines) {
            if (!corrupted && l.rfind("TRADE|", 0) == 0) {
                // Fields: TRADE|id|aggressor|resting|side|price|qty|ts|seq
                std::size_t pos = 0;
                int pipe_count = 0;
                while (pipe_count < 6 && pos != std::string::npos) {
                    pos = l.find('|', pos + 1);
                    ++pipe_count;
                }
                const std::size_t qty_start = pos + 1;
                const std::size_t qty_end = l.find('|', qty_start);
                l = l.substr(0, qty_start) + "999999" + l.substr(qty_end);
                corrupted = true;
            }
            out << l << '\n';
        }
        ASSERT_TRUE(corrupted);
    }

    ReplayResult result = replay_from_file(path);

    EXPECT_FALSE(result.matched);
    EXPECT_FALSE(result.detail.empty());

    std::filesystem::remove(path);
}

TEST(Replay, DetectsFinalStateHashDivergence) {
    const std::string path = temp_log_path("corrupt_hash");

    MatchingEngine recorded_engine;
    CommandLogWriter writer(path);
    auto recorded_trades = run_reference_sequence(recorded_engine, writer);
    // Deliberately record a wrong final hash.
    writer.finish(recorded_trades, recorded_engine.state_hash() ^ 0xDEADBEEFULL);

    ReplayResult result = replay_from_file(path);

    EXPECT_FALSE(result.matched);
    EXPECT_EQ(result.divergent_sequence, 0u);
    EXPECT_FALSE(result.detail.empty());

    std::filesystem::remove(path);
}

TEST(Replay, SequenceNumbersAreDeterministicAcrossRuns) {
    const std::string path_a = temp_log_path("determinism_a");
    const std::string path_b = temp_log_path("determinism_b");

    MatchingEngine engine_a;
    CommandLogWriter writer_a(path_a);
    auto trades_a = run_reference_sequence(engine_a, writer_a);
    writer_a.finish(trades_a, engine_a.state_hash());

    MatchingEngine engine_b;
    CommandLogWriter writer_b(path_b);
    auto trades_b = run_reference_sequence(engine_b, writer_b);
    writer_b.finish(trades_b, engine_b.state_hash());

    ASSERT_EQ(trades_a.size(), trades_b.size());
    for (std::size_t i = 0; i < trades_a.size(); ++i) {
        EXPECT_EQ(trades_a[i].sequence, trades_b[i].sequence);
    }
    EXPECT_EQ(engine_a.state_hash(), engine_b.state_hash());

    std::filesystem::remove(path_a);
    std::filesystem::remove(path_b);
}
