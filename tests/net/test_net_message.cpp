#include <catch2/catch_test_macros.hpp>
#include "net/net_message.hpp"
#include "net/net_buffer.hpp"
#include "net/net_types.hpp"
#include <memory>

using simnet::core::net::internal::NetMessage;
using simnet::core::net::internal::HelloMessage;
using simnet::core::net::internal::WelcomeMessage;
using simnet::core::net::internal::RejectMessage;
using simnet::core::net::internal::MessageType;
using simnet::core::net::internal::RejectReason;
using simnet::core::net::internal::ProtocolVersion;
using simnet::core::net::internal::NetBuffer;
using simnet::core::net::internal::CURRENT_PROTOCOL_VERSION;

TEST_CASE("NetMessage: Hello roundtrip", "[net_message]")
{
    NetBuffer buf;
    HelloMessage hello(CURRENT_PROTOCOL_VERSION);
    hello.serialize(buf);

    auto msg = NetMessage::deserialize(buf);
    REQUIRE(msg != nullptr);
    auto *hello2 = dynamic_cast<HelloMessage *>(msg.get());
    REQUIRE(hello2 != nullptr);
    REQUIRE(hello2->get_version() == CURRENT_PROTOCOL_VERSION);
}

TEST_CASE("NetMessage: Welcome roundtrip", "[net_message]")
{
    NetBuffer buf;
    WelcomeMessage welcome;
    welcome.serialize(buf);

    auto msg = NetMessage::deserialize(buf);
    REQUIRE(msg != nullptr);
    REQUIRE(dynamic_cast<WelcomeMessage*>(msg.get()) != nullptr);
}

TEST_CASE("NetMessage: Reject roundtrip", "[net_message]")
{
    NetBuffer buf;
    RejectMessage reject(RejectReason::ServerFull);
    reject.serialize(buf);

    auto msg = NetMessage::deserialize(buf);
    REQUIRE(msg != nullptr);
    auto *reject2 = dynamic_cast<RejectMessage *>(msg.get());
    REQUIRE(reject2 != nullptr);
    REQUIRE(reject2->get_reason() == RejectReason::ServerFull);
}

TEST_CASE("NetMessage: deserialize truncated Hello", "[net_message]")
{
    NetBuffer buf;
    buf.write(static_cast<uint8_t>(MessageType::Hello));
    REQUIRE(NetMessage::deserialize(buf) == nullptr);
}

TEST_CASE("NetMessage: deserialize truncated Reject", "[net_message]")
{
    NetBuffer buf;
    buf.write(static_cast<uint8_t>(MessageType::Reject));
    REQUIRE(NetMessage::deserialize(buf) == nullptr);
}

TEST_CASE("NetMessage: deserialize unknown type", "[net_message]")
{
    NetBuffer buf;
    buf.write(static_cast<uint8_t>(0xFF)); // invalid
    REQUIRE(NetMessage::deserialize(buf) == nullptr);
}
