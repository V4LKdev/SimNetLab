## CMake
A split CMake structure cleanly seperates target-dependent dependency resolution from platform-independent project logic.
CMake's official documentation recommends using modular add_subdirectory() layouts to isolate target definitions and dependency discovery per component.

## Packages
This setup follows the vcpkg + cmake integration model, where vcpkg provides stable cross-platform dependency management via CMake package configs (find_package) instead of manual linking.

On linux, system packages are preferred for raylib and GLFW-dependent stacks, because based on distribution and display server, glfw3 has known specific linking requirements. All mayor linux distributions have a correctly integrated GLFW/raylib stack in their package manager.

### List of Packages and how they are included

| Library | Linux                   | Windows / Mac OS |
| ------- | ----------------------- | ---------------- |
| raylib  | pacman                  | vcpkg            |
| enet    | pacman (vcpkg fallback) | vcpkg            |
| flecs   | vcpkg                   | vcpkg            |
