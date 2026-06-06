#include <iostream>
#include <raylib.h>

#include "controller.hpp"
#include "simulation.hpp"

int main() {

    simnet::sim::Simulation sim;
    simnet::core::TimestepController controller(sim);

    InitWindow(800, 450, "SimNetLab_Client");
    SetTargetFPS(0);

    while (!WindowShouldClose()) {

        const int steps = controller.update();
        const double interp_alpha = controller.get_interpolation_alpha();

        BeginDrawing();
        ClearBackground(DARKGRAY);

        DrawText(TextFormat("steps: %d", steps), 20, 20, 24, BLUE);
        DrawText(TextFormat("alpha: %f", interp_alpha), 20, 50, 24, BLUE);
        DrawText(TextFormat("tick: %llu", sim.current_tick()), 20, 80, 24, BLUE);

        EndDrawing();
    }

    CloseWindow();
    return 0;
}
