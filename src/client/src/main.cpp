#include <chrono>
#include <iostream>
#include <enet/enet.h>
#include <flecs.h>
#include <raylib.h>

void simulation() {

}

constexpr int SIM_HZ = 30;
constexpr std::chrono::nanoseconds SIM_DT = std::chrono::nanoseconds(1'000'000'000 / SIM_HZ);
constexpr int MAX_SIM_STEPS = 5;
constexpr auto MAX_ACCUM_NS = SIM_DT.count() * MAX_SIM_STEPS; // Spiral-of-death capping

int main() {
    using Clock = std::chrono::steady_clock;

    uint64_t sim_time_ns = 0;
    int64_t accumulator_ns = 0;
    std::chrono::time_point<Clock> last_time = Clock::now();

    InitWindow(800, 450, "SimNetLab_Client");
    SetTargetFPS(0);

    while (!WindowShouldClose()) {

        std::chrono::time_point<Clock> now = Clock::now();
        std::chrono::nanoseconds frame_delta =
            std::chrono::duration_cast<std::chrono::nanoseconds>(now - last_time);
        last_time = now;

        // accumulate time but cap for spiral prevention
        accumulator_ns += frame_delta.count();
        accumulator_ns = std::min(accumulator_ns, MAX_ACCUM_NS);

        // Simulation Loop
        while (accumulator_ns >= SIM_DT.count())
        {
            sim_time_ns += SIM_DT.count();
            accumulator_ns -= SIM_DT.count();

            simulation();
        }

        BeginDrawing();
        ClearBackground(DARKGRAY);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}