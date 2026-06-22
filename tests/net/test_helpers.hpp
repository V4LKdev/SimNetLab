#pragma once
#include "core/utils/time_keeper.hpp"

inline simnet::core::utils::TimePoint make_time(int ms_from_start)
{
    return simnet::core::utils::TimePoint{} + std::chrono::milliseconds(ms_from_start);
}
