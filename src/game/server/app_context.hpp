#pragma once
#include "core/core.hpp"

namespace simnet::game::server {
    struct AppContext {
        net::NetManager *net = nullptr;
    };
}
