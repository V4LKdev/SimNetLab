#include <iostream>
#include <enet/enet.h>
#include <flecs.h>

int main() {

    if (enet_initialize() != 0) {
        std::cerr << "An error occurred while initializing ENet.\n";
        return EXIT_FAILURE;
    }
    atexit(enet_deinitialize);

    flecs::world world;
    std::cout << "Flecs world created successfully. Standing by.\n";

    return 0;
}