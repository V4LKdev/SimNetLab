#pragma once
#include <memory>

#include "net_buffer.hpp"
#include "net_types.hpp"
#include "telemetry.hpp"

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

    // --- Factory ---
    inline std::unique_ptr<NetMessage> NetMessage::deserialize(NetBuffer &buffer)
    {
        if (buffer.remaining() < 1) {
            return nullptr;
        }

        auto type = static_cast<MessageType>(buffer.read<uint8_t>());

        std::unique_ptr<NetMessage> result = nullptr;

        switch (type) {
            case MessageType::Hello: {
                if (buffer.remaining() < HELLO_SIZE) {
                    return nullptr;
                }
                ProtocolVersion version = buffer.read<uint16_t>();
                result = std::make_unique<HelloMessage>(version);
                break;
            }
            case MessageType::Welcome:
                result = std::make_unique<WelcomeMessage>();
                break;
            case MessageType::Reject: {
                if (buffer.remaining() < REJECT_SIZE) {
                    return nullptr;
                }
                auto reason = static_cast<RejectReason>(buffer.read<uint8_t>());
                result = std::make_unique<RejectMessage>(reason);
                break;
            }

            default:
                TELEM_LOG_WARN("NetMessage::deserialize unknown type {}", static_cast<int>(type));
                TELEM_COUNTER_INC("net.control_unknown_type", 1);
                return nullptr;
        }

        if (result) {
            std::string counter_name = fmt::format("net.control_in_{}", static_cast<int>(type));
            telemetry::MetricsCollector::instance().add_counter(counter_name, 1);
            TELEM_COUNTER_INC("net.control_total", 1);
        } else {
            TELEM_COUNTER_INC("net.control_parse_error", 1);
        }
        return result;
    }
}
