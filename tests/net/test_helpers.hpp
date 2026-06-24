#pragma once
#include "core/utils/time_keeper.hpp"
#include "core/config/sim_config.hpp"
#include "core/net/net_buffer.hpp"
#include "core/net/net_types.hpp"

inline simnet::core::utils::TimePoint make_time(int ms_from_start)
{
    return simnet::core::utils::TimePoint{} + std::chrono::milliseconds(ms_from_start);
}

inline simnet::core::config::SimConfig default_test_config()
{
    return simnet::core::config::SimConfig::default_config();
}

inline uint64_t default_test_fingerprint()
{
    return default_test_config().fingerprint();
}

inline NetBuffer build_hello_buf(
    simnet::core::net::internal::ProtocolVersion ver,
    uint64_t fingerprint)
{
    NetBuffer buf;
    using simnet::core::net::internal::MessageType;
    buf.write(static_cast<uint8_t>(MessageType::Hello));
    buf.write(ver);
    buf.write(fingerprint);
    return buf;
}
