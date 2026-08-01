#include <catch2/catch_test_macros.hpp>

#include <vector>

import simnet.core;
import simnet.snapshot;
import simnet.synthetic;

TEST_CASE("synthetic snapshots use deterministic nonzero entity ids", "[synthetic][snapshot]")
{
    auto settings = simnet::SyntheticSnapshotSettings{};
    settings.entity_count = 3U;

    auto const snapshot = simnet::make_synthetic_world_snapshot(settings, 7U);

    CHECK(snapshot.ids == std::vector<simnet::EntityNetId>{1U, 2U, 3U});
    CHECK(simnet::validate_world_snapshot(snapshot).valid);
}
