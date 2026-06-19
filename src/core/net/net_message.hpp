#pragma once
#include <memory>

#include "net_buffer.hpp"
#include "net_types.hpp"

namespace simnet::core::net::internal {
    // --- Base class for all control messages ---
    class NetMessage {
    public:
        NetMessage() = default;

        virtual ~NetMessage() = default;

        NetMessage(const NetMessage &) = default;

        NetMessage &operator=(const NetMessage &) = default;

        NetMessage(NetMessage &&) = delete;

        NetMessage &operator=(NetMessage &&) = delete;

        /// Write the message header + payload into the buffer.
        virtual void serialize(NetBuffer &buffer) const = 0;

        /// Read from buffer at current read position, return nullptr if invalid
        static std::unique_ptr<NetMessage> deserialize(NetBuffer &buffer);
    };

    // --- Hello Message ---
    class HelloMessage final : public NetMessage {
    public:
        explicit HelloMessage(ProtocolVersion version) : version_(version)
        {
        }

        [[nodiscard]] ProtocolVersion get_version() const { return version_; }

        void serialize(NetBuffer &buffer) const override
        {
            buffer.write(static_cast<uint8_t>(MessageType::Hello));
            buffer.write(version_);
        }

    private:
        ProtocolVersion version_;
    };

    // --- Welcome Message ---
    class WelcomeMessage final : public NetMessage {
    public:
        void serialize(NetBuffer &buffer) const override
        {
            buffer.write(static_cast<uint8_t>(MessageType::Welcome));
        }
    };

    // --- Reject Message ---
    class RejectMessage final : public NetMessage {
    public:
        explicit RejectMessage(RejectReason reason) : reason_(reason)
        {
        }

        [[nodiscard]] RejectReason get_reason() const { return reason_; }

        void serialize(NetBuffer &buffer) const override
        {
            buffer.write(static_cast<uint8_t>(MessageType::Reject));
            buffer.write(static_cast<uint8_t>(reason_));
        }

    private:
        RejectReason reason_;
    };

    // --- Ping Message ---
    class PingMessage final : public NetMessage {
    public:
        void serialize(NetBuffer &buffer) const override
        {
            buffer.write(static_cast<uint8_t>(MessageType::Ping));
        }
    };

    // --- Pong Message ---
    class PongMessage final : public NetMessage {
    public:
        void serialize(NetBuffer &buffer) const override
        {
            buffer.write(static_cast<uint8_t>(MessageType::Pong));
        }
    };


    // --- Factory ---
    inline std::unique_ptr<NetMessage> NetMessage::deserialize(NetBuffer &buffer)
    {
        if (buffer.remaining() < 1) {
            return nullptr;
        }

        auto type = static_cast<MessageType>(buffer.read<uint8_t>());

        switch (type) {
            case MessageType::Hello: {
                if (buffer.remaining() < HELLO_SIZE) {
                    return nullptr;
                }
                ProtocolVersion version = buffer.read<uint16_t>();
                return std::make_unique<HelloMessage>(version);
            }
            case MessageType::Welcome:
                return std::make_unique<WelcomeMessage>();
            case MessageType::Reject: {
                if (buffer.remaining() < REJECT_SIZE) {
                    return nullptr;
                }
                auto reason = static_cast<RejectReason>(buffer.read<uint8_t>());
                return std::make_unique<RejectMessage>(reason);
            }
            case MessageType::Ping:
                return std::make_unique<PingMessage>();

            case MessageType::Pong:
                return std::make_unique<PongMessage>();

            default:
                return nullptr;
        }
    }
}
