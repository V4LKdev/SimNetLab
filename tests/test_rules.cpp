// #include <catch2/catch_test_macros.hpp>
// #include <catch2/catch_approx.hpp>
//
// #include <cmath>
// #include <cstdint>
// #include <vector>
//
// #include "math/vec3.hpp"
// #include "math/rules_scalar.hpp"
//
// using simnet::Vec3;
// using Catch::Approx;
//
// namespace {
//     // Helper: compare two Vec3 within epsilon
//     bool vec3_approx(const Vec3 &a, const Vec3 &b, float eps = 1e-5f)
//     {
//         auto near = [eps](float lhs, float rhs) {
//             return std::abs(lhs - rhs) <= eps;
//         };
//         return near(a.x(), b.x()) && near(a.y(), b.y()) && near(a.z(), b.z());
//     }
//
//     // Commonly used data arrays (empty by default)
//     std::vector<Vec3> positions;
//     std::vector<Vec3> headings;
//     std::vector<uint32_t> neighbor_indices; // indices into positions/headings
//
//     // Helper to clear and reset test data
//     void reset_data()
//     {
//         positions.clear();
//         headings.clear();
//         neighbor_indices.clear();
//     }
// }
//
// // compute_alignment
// TEST_CASE("Alignment: zero neighbours", "[alignment]")
// {
//     reset_data();
//     const float world_half = 50.f;
//     const Vec3 self_pos{0, 0, 0};
//     const Vec3 self_heading{1, 0, 0};
//
//     const Vec3 result = simnet::scalar::compute_alignment(
//         self_pos, self_heading,
//         neighbor_indices.data(), neighbor_indices.size(),
//         positions.data(), headings.data(),
//         10.0f,
//         -0.5f,
//         world_half
//     );
//
//     REQUIRE(vec3_approx(result, Vec3::zero()));
// }
//
//
// TEST_CASE("Alignment: single neighbour with identical heading", "[alignment]")
// {
//     reset_data();
//     const float world_half = 50.f;
//     positions.push_back({2.0f, 0.0f, 0.0f}); // index 0
//     headings.push_back({1.0f, 0.0f, 0.0f}); // same heading
//     neighbor_indices = {0};
//
//     const Vec3 self_pos{0, 0, 0};
//     const Vec3 self_heading{1, 0, 0};
//
//     const Vec3 result = simnet::scalar::compute_alignment(
//         self_pos, self_heading,
//         neighbor_indices.data(), neighbor_indices.size(),
//         positions.data(), headings.data(),
//         10.0f,
//         -0.5f, world_half
//     );
//
//     // Average heading = self heading => difference zero
//     REQUIRE(vec3_approx(result, Vec3::zero()));
// }
//
// TEST_CASE("Alignment: single neighbour facing opposite", "[alignment]")
// {
//     reset_data();
//     const float world_half = 50.f;
//     positions.push_back({2.0f, 0.0f, 0.0f});
//     headings.push_back({-1.0f, 0.0f, 0.0f});
//     neighbor_indices = {0};
//
//     const Vec3 self_pos{0, 0, 0};
//     const Vec3 self_heading{1, 0, 0};
//
//     const Vec3 result = simnet::scalar::compute_alignment(
//         self_pos, self_heading,
//         neighbor_indices.data(), neighbor_indices.size(),
//         positions.data(), headings.data(),
//         10.0f, -0.5f, world_half
//     );
//
//     // Avg heading = (1 + -1)/2 = 0 => steering = -self_heading normalized = (-1,0,0)
//     const Vec3 expected{-1.0f, 0.0f, 0.0f};
//     REQUIRE(vec3_approx(result, expected));
// }
//
// TEST_CASE("Alignment: two neighbours, one aligned, one orthogonal", "[alignment]")
// {
//     reset_data();
//     const float world_half = 50.f;
//     positions.push_back({1.0f, 0.0f, 0.0f}); // index 0
//     headings.push_back({1.0f, 0.0f, 0.0f}); // aligned
//     positions.push_back({0.0f, 1.0f, 0.0f}); // index 1
//     headings.push_back({0.0f, 1.0f, 0.0f}); // orthogonal
//     neighbor_indices = {0, 1};
//
//     const Vec3 self_pos{0, 0, 0};
//     const Vec3 self_heading{1, 0, 0};
//
//     const Vec3 result = simnet::scalar::compute_alignment(
//         self_pos, self_heading,
//         neighbor_indices.data(), neighbor_indices.size(),
//         positions.data(), headings.data(),
//         10.0f, -0.5f, world_half
//     );
//
//     // Avg heading = (1+0, 0+1, 0) = (1,1,0) / 2 => (0.5,0.5,0)
//     // steering = (0.5,0.5,0) - (1,0,0) = (-0.5,0.5,0) -> normalised
//     const float inv_len = 1.0f / std::sqrt(0.5f); // length = sqrt(0.25+0.25)=sqrt(0.5)
//     const Vec3 expected{-0.5f * inv_len, 0.5f * inv_len, 0.0f};
//     REQUIRE(vec3_approx(result, expected));
// }
//
// TEST_CASE("Alignment: neighbour outside radius is ignored", "[alignment]")
// {
//     reset_data();
//     const float world_half = 50.f;
//     positions.push_back({100.0f, 0.0f, 0.0f}); // far outside radius 5
//     headings.push_back({0.0f, 1.0f, 0.0f}); // irrelevant
//     neighbor_indices = {0};
//
//     const Vec3 self_pos{0, 0, 0};
//     const Vec3 self_heading{1, 0, 0};
//
//     const Vec3 result = simnet::scalar::compute_alignment(
//         self_pos, self_heading,
//         neighbor_indices.data(), neighbor_indices.size(),
//         positions.data(), headings.data(),
//         5.0f,
//         -0.5f, world_half
//     );
//
//     // filtered => no neighbours => zero
//     REQUIRE(vec3_approx(result, Vec3::zero()));
// }
//
// // compute_cohesion
// TEST_CASE("Cohesion: zero neighbours", "[cohesion]")
// {
//     const float world_half = 50.f;
//     const Vec3 self_pos{0, 0, 0};
//     const Vec3 self_heading{1, 0, 0};
//
//     const Vec3 result = simnet::scalar::compute_cohesion(
//         self_pos, self_heading,
//         nullptr, 0,
//         positions.data(), headings.data(),
//         10.0f,
//         -0.5f, world_half
//     );
//
//     REQUIRE(vec3_approx(result, Vec3::zero()));
// }
//
// TEST_CASE("Cohesion: one neighbour → steer towards it", "[cohesion]")
// {
//     reset_data();
//     const float world_half = 50.f;
//     positions.push_back({3.0f, 0.0f, 0.0f}); // neighbour to the right
//     headings.push_back({0, 0, 0}); // unused
//     neighbor_indices = {0};
//
//     const Vec3 self_pos{0, 0, 0};
//     const Vec3 self_heading{1, 0, 0};
//
//     const Vec3 result = simnet::scalar::compute_cohesion(
//         self_pos, self_heading,
//         neighbor_indices.data(), neighbor_indices.size(),
//         positions.data(), headings.data(),
//         10.0f,
//         -0.5f,
//         world_half
//     );
//
//     // The function sums self_pos + delta for each neighbour, i.e., nb_pos
//     // then avg = nb_pos / 1 = nb_pos, steer = (avg - self_pos).normalized() = (3,0,0) -> (1,0,0)
//     const Vec3 expected{1.0f, 0.0f, 0.0f};
//     REQUIRE(vec3_approx(result, expected));
// }
//
// TEST_CASE("Cohesion: two symmetric neighbours → zero", "[cohesion]")
// {
//     reset_data();
//     const float world_half = 50.f;
//     positions.push_back({2.0f, 0.0f, 0.0f}); // +X
//     positions.push_back({-2.0f, 0.0f, 0.0f}); // -X
//     headings = {{0, 0, 0}, {0, 0, 0}};
//     neighbor_indices = {0, 1};
//
//     const Vec3 self_pos{0, 0, 0};
//     const Vec3 self_heading{0, 0, 1};
//
//     const Vec3 result = simnet::scalar::compute_cohesion(
//         self_pos, self_heading,
//         neighbor_indices.data(), neighbor_indices.size(),
//         positions.data(), headings.data(),
//         10.0f, -0.5f, world_half
//     );
//
//     REQUIRE(vec3_approx(result, Vec3::zero()));
// }
//
// TEST_CASE("Cohesion: two neighbours on same side", "[cohesion]")
// {
//     reset_data();
//     const float world_half = 50.f;
//     positions.push_back({1.0f, 0.0f, 0.0f}); // near
//     positions.push_back({3.0f, 0.0f, 0.0f}); // far
//     headings = {{0, 0, 0}, {0, 0, 0}};
//     neighbor_indices = {0, 1};
//
//     const Vec3 self_pos{0, 0, 0};
//     const Vec3 self_heading{1, 0, 0};
//
//     const Vec3 result = simnet::scalar::compute_cohesion(
//         self_pos, self_heading,
//         neighbor_indices.data(), neighbor_indices.size(),
//         positions.data(), headings.data(),
//         10.0f, -0.5f, world_half
//     );
//
//     // Sum = self+(1,0,0) + self+(3,0,0) = (4,0,0) -> avg = (2,0,0)
//     // steer = (2,0,0) - (0,0,0) = (2,0,0) -> normalized (1,0,0)
//     const Vec3 expected{1.0f, 0.0f, 0.0f};
//     REQUIRE(vec3_approx(result, expected));
// }
//
// // compute_separation
// TEST_CASE("Separation: zero neighbours", "[separation]")
// {
//     const float world_half = 50.f;
//     const Vec3 self_pos{0, 0, 0};
//     const Vec3 self_heading{1, 0, 0};
//
//     const Vec3 result = simnet::scalar::compute_separation(
//         self_pos, self_heading,
//         nullptr, 0,
//         positions.data(), headings.data(),
//         10.0f, -0.5f,
//         world_half
//     );
//
//     REQUIRE(vec3_approx(result, Vec3::zero()));
// }
//
// TEST_CASE("Separation: very close neighbour → push away", "[separation]")
// {
//     reset_data();
//     const float world_half = 50.f;
//     positions.push_back({0.1f, 0.0f, 0.0f}); // very close
//     headings.push_back({0, 0, 0}); // unused
//     neighbor_indices = {0};
//
//     const Vec3 self_pos{0, 0, 0};
//     const Vec3 self_heading{1, 0, 0};
//
//     const Vec3 result = simnet::scalar::compute_separation(
//         self_pos, self_heading,
//         neighbor_indices.data(), neighbor_indices.size(),
//         positions.data(), headings.data(),
//         10.0f,
//         -1.0f,
//         world_half
//     );
//
//     // to_neighbour = (0.1,0,0), d_sq = 0.01
//     // contribution = (0.1,0,0) / -0.01 = (-10,0,0)
//     // normalized = (-1,0,0)
//     const Vec3 expected{-1.0f, 0.0f, 0.0f};
//     REQUIRE(vec3_approx(result, expected));
// }
//
// TEST_CASE("Separation: neighbour beyond radius is ignored", "[separation]")
// {
//     reset_data();
//     const float world_half = 50.f;
//     positions.push_back({50.0f, 0.0f, 0.0f}); // far
//     headings.push_back({0, 0, 0});
//     neighbor_indices = {0};
//
//     const Vec3 self_pos{0, 0, 0};
//     const Vec3 self_heading{1, 0, 0};
//
//     const Vec3 result = simnet::scalar::compute_separation(
//         self_pos, self_heading,
//         neighbor_indices.data(), neighbor_indices.size(),
//         positions.data(), headings.data(),
//         5.0f,
//         -1.0f,
//         world_half
//     );
//
//     // filtered => zero
//     REQUIRE(vec3_approx(result, Vec3::zero()));
// }
//
// TEST_CASE("Separation: two opposite neighbours cancel", "[separation]")
// {
//     reset_data();
//     const float world_half = 50.f;
//     positions.push_back({1.0f, 0.0f, 0.0f});
//     positions.push_back({-1.0f, 0.0f, 0.0f});
//     headings = {{0, 0, 0}, {0, 0, 0}};
//     neighbor_indices = {0, 1};
//
//     const Vec3 self_pos{0, 0, 0};
//     const Vec3 self_heading{0, 0, 1};
//
//     const Vec3 result = simnet::scalar::compute_separation(
//         self_pos, self_heading,
//         neighbor_indices.data(), neighbor_indices.size(),
//         positions.data(), headings.data(),
//         10.0f, -1.0f, world_half
//     );
//
//     // contributions: right push left (-1,0,0), left push right (1,0,0) => sum zero
//     REQUIRE(vec3_approx(result, Vec3::zero()));
// }
//
// TEST_CASE("Separation: two neighbours, one closer → stronger push", "[separation]")
// {
//     reset_data();
//     const float world_half = 50.f;
//     // close neighbour at (0.2, 0, 0): d_sq = 0.04, contribution = (0.2 / -0.04) = (-5, 0, 0)
//     // far neighbour at (1.0, 0, 0):   d_sq = 1.0, contribution = (1.0 / -1.0) = (-1, 0, 0)
//     positions.push_back({0.2f, 0.0f, 0.0f});
//     positions.push_back({1.0f, 0.0f, 0.0f});
//     headings = {{0, 0, 0}, {0, 0, 0}};
//     neighbor_indices = {0, 1};
//
//     const Vec3 self_pos{0, 0, 0};
//     const Vec3 self_heading{1, 0, 0};
//
//     const Vec3 result = simnet::scalar::compute_separation(
//         self_pos, self_heading,
//         neighbor_indices.data(), neighbor_indices.size(),
//         positions.data(), headings.data(),
//         10.0f, -1.0f, world_half
//     );
//
//     // sum = (-5,0,0) + (-1,0,0) = (-6,0,0) -> normalized = (-1,0,0)
//     const Vec3 expected{-1.0f, 0.0f, 0.0f};
//     REQUIRE(vec3_approx(result, expected));
// }
//
// // apply_steering
// TEST_CASE("ApplySteering: zero steering, velocity unchanged", "[steering]")
// {
//     Vec3 vel{3.0f, 4.0f, 0.0f}; // speed 5
//     const Vec3 steer{0, 0, 0};
//     const float dt = 1.0f;
//     const float max_accel = 10.0f;
//     const float max_speed = 10.0f;
//
//     vel = simnet::scalar::apply_steering(steer, vel, max_accel, max_speed, dt);
//     // should be unchanged
//     REQUIRE(vec3_approx(vel, Vec3{3.0f, 4.0f, 0.0f}));
// }
//
// TEST_CASE("ApplySteering: small steering applied", "[steering]")
// {
//     Vec3 vel{0, 0, 0};
//     const Vec3 steer{1.0f, 0.0f, 0.0f};
//     const float dt = 2.0f;
//     const float max_accel = 10.0f; // steer length 1, not clipped
//     const float max_speed = 10.0f;
//
//     vel = simnet::scalar::apply_steering(steer, vel, max_accel, max_speed, dt);
//     // new vel = 0 + (1,0,0)*2 = (2,0,0)
//     REQUIRE(vec3_approx(vel, Vec3{2.0f, 0.0f, 0.0f}));
// }
//
// TEST_CASE("ApplySteering: steering exceeds max accel", "[steering]")
// {
//     Vec3 vel{0, 0, 0};
//     const Vec3 steer{6.0f, 8.0f, 0.0f}; // length 10
//     const float dt = 1.0f;
//     const float max_accel = 5.0f;
//     const float max_speed = 20.0f;
//
//     vel = simnet::scalar::apply_steering(steer, vel, max_accel, max_speed, dt);
//     // steer truncated to length 5 (direction (6,8,0) normalized = (0.6,0.8,0) * 5 = (3,4,0)
//     // vel = (3,4,0)
//     REQUIRE(vec3_approx(vel, Vec3{3.0f, 4.0f, 0.0f}));
// }
//
// TEST_CASE("ApplySteering: result capped to max speed", "[steering]")
// {
//     Vec3 vel{9.0f, 0.0f, 0.0f}; // speed 9
//     const Vec3 steer{4.0f, 0.0f, 0.0f}; // length 4
//     const float dt = 1.0f;
//     const float max_accel = 10.0f;
//     const float max_speed = 10.0f;
//
//     vel = simnet::scalar::apply_steering(steer, vel, max_accel, max_speed, dt);
//     // new vel = (13,0,0) length 13 > max_speed 10, clamped to (10,0,0)
//     REQUIRE(vec3_approx(vel, Vec3{10.0f, 0.0f, 0.0f}));
// }
//
// TEST_CASE("ApplySteering: both accel and speed clamped", "[steering]")
// {
//     Vec3 vel{5.0f, 5.0f, 0.0f}; // speed ~7.07
//     const Vec3 steer{10.0f, 0.0f, 0.0f}; // length 10
//     const float dt = 1.0f;
//     const float max_accel = 2.0f;
//     const float max_speed = 8.0f;
//
//     vel = simnet::scalar::apply_steering(steer, vel, max_accel, max_speed, dt);
//     // steer truncated to (2,0,0) (since max_accel=2)
//     // new vel = (7,5,0), length = sqrt(49+25) = sqrt(74) ≈ 8.602 > 8, clamp to length 8
//     // final direction (7/8.602, 5/8.602, 0) * 8 ≈ (6.505, 4.646, 0)
//     const float len = std::sqrt(7.0f * 7.0f + 5.0f * 5.0f);
//     const Vec3 expected{7.0f / len * 8.0f, 5.0f / len * 8.0f, 0.0f};
//     REQUIRE(vec3_approx(vel, expected, 1e-4f));
// }
//
// // Edge cases
// TEST_CASE("Alignment: self heading zero → all neighbours filtered", "[alignment]")
// {
//     reset_data();
//     const float world_half = 50.f;
//     positions.push_back({2.0f, 0.0f, 0.0f});
//     headings.push_back({1.0f, 0.0f, 0.0f});
//     neighbor_indices = {0};
//
//     const Vec3 self_pos{0, 0, 0};
//     const Vec3 self_heading{0, 0, 0}; // zero heading
//
//     const Vec3 result = simnet::scalar::compute_alignment(
//         self_pos, self_heading,
//         neighbor_indices.data(), neighbor_indices.size(),
//         positions.data(), headings.data(),
//         10.0f,
//         0.5f,
//         world_half
//     );
//
//     // No valid neighbours → zero
//     REQUIRE(vec3_approx(result, Vec3::zero()));
// }
//
// TEST_CASE("Separation: neighbour exactly at 0.001 distance is ignored", "[separation]")
// {
//     reset_data();
//     const float world_half = 50.f;
//     // Distance = 0.001, d_sq = 1e-6, which is NOT > 0.001^2 (1e-6) so skipped
//     positions.push_back({0.001f, 0.0f, 0.0f});
//     headings.push_back({0, 0, 0});
//     neighbor_indices = {0};
//
//     const Vec3 self_pos{0, 0, 0};
//     const Vec3 self_heading{1, 0, 0};
//
//     const Vec3 result = simnet::scalar::compute_separation(
//         self_pos, self_heading,
//         neighbor_indices.data(), neighbor_indices.size(),
//         positions.data(), headings.data(),
//         10.0f,
//         -1.0f,
//         world_half
//     );
//
//     REQUIRE(vec3_approx(result, Vec3::zero()));
// }
//
// TEST_CASE("Cohesion: neighbour behind FOV is filtered", "[cohesion]")
// {
//     reset_data();
//     const float world_half = 50.f;
//     // Neighbour directly behind (-X), self heading +X => forwardness = -1
//     positions.push_back({-2.0f, 0.0f, 0.0f});
//     headings.push_back({0, 0, 0});
//     neighbor_indices = {0};
//
//     const Vec3 self_pos{0, 0, 0};
//     const Vec3 self_heading{1, 0, 0};
//
//     const Vec3 result = simnet::scalar::compute_cohesion(
//         self_pos, self_heading,
//         neighbor_indices.data(), neighbor_indices.size(),
//         positions.data(), headings.data(),
//         10.0f,
//         0.5f,
//         world_half
//     );
//
//     REQUIRE(vec3_approx(result, Vec3::zero()));
// }
//
// TEST_CASE("ApplySteering: max_speed = 0 clamps to zero", "[steering]")
// {
//     Vec3 vel{5.0f, 0.0f, 0.0f};
//     const Vec3 steer{10.0f, 0.0f, 0.0f};
//     const float dt = 1.0f;
//     const float max_accel = 100.0f;
//     const float max_speed = 0.0f;
//
//     vel = simnet::scalar::apply_steering(steer, vel, max_accel, max_speed, dt);
//     // speed clamped to 0 → zero vector
//     REQUIRE(vec3_approx(vel, Vec3::zero()));
// }
//
// TEST_CASE("ApplySteering: zero initial velocity with small steering", "[steering]")
// {
//     Vec3 vel{0, 0, 0};
//     const Vec3 steer{0.5f, 0.5f, 0.0f};
//     const float dt = 2.0f;
//     const float max_accel = 10.0f;
//     const float max_speed = 10.0f;
//
//     vel = simnet::scalar::apply_steering(steer, vel, max_accel, max_speed, dt);
//     // new vel = (0.5,0.5)*2 = (1,1,0)
//     REQUIRE(vec3_approx(vel, Vec3{1.0f, 1.0f, 0.0f}));
// }
