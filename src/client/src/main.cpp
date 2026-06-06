#include <iostream>
#include <enet/enet.h>
#include <flecs.h>
#include <raylib.h>

int main() {

    InitWindow(800, 450, "Thesis Client Dev Window");
    SetTargetFPS(60);

    while (!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground(RAYWHITE);

        DrawText("Hello, World!", 190, 200, 20, DARKGRAY);
        DrawRectangle(10, 10, 50, 50, RED);

        EndDrawing();
    }

    CloseWindow();
    return 0;
}