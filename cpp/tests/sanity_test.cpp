#include <gtest/gtest.h>

#include "order.hpp"

// Placeholder to confirm the build/test toolchain wires up end to end.
// Real coverage (book behavior, matching, lifecycle, TIF, edge cases,
// randomized invariant tests) is added starting Milestone 1.
TEST(Sanity, ToolchainBuildsAndRuns) {
    lob::Order order{};
    order.quantity = 100;
    EXPECT_EQ(order.quantity, 100u);
}
