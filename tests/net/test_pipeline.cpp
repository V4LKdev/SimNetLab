#include <catch2/catch_test_macros.hpp>
#include "net/net_pipeline.hpp"
#include "net/net_processor.hpp"
#include "net/net_buffer.hpp"
#include <memory>
#include <vector>

using simnet::core::net::internal::NetPipeline;
using simnet::core::net::internal::NetProcessor;
using simnet::core::net::internal::PeerState;
using simnet::core::net::internal::NetBuffer;
using simnet::core::net::internal::SnapshotFlags;

// ------------------------------------------------------------------
// Processor that appends a single byte on outgoing,
// and on incoming reads the next byte (expected to match the appended byte).
// Used for the round‑trip test.
// ------------------------------------------------------------------
class AppendByteProcessor final : public NetProcessor {
public:
    explicit AppendByteProcessor(uint8_t byte) : m_byte(byte)
    {
    }

    void process_outgoing(PeerState &, NetBuffer &buf, SnapshotFlags) override
    {
        buf.write(m_byte);
    }

    void process_incoming(PeerState &, NetBuffer &buf, SnapshotFlags) override
    {
        REQUIRE(buf.remaining() >= 1);
        auto val = buf.read<uint8_t>();
        REQUIRE(val == m_byte);
    }

private:
    uint8_t m_byte;
};

// ------------------------------------------------------------------
// Spy processor that only records call order.
// Used for the multiple‑processor ordering test.
// ------------------------------------------------------------------
class CallOrderProcessor final : public NetProcessor {
public:
    CallOrderProcessor(int id, std::vector<int> &out, std::vector<int> &in)
        : m_id(id), m_out(out), m_in(in)
    {
    }

    void process_outgoing(PeerState &, NetBuffer &, SnapshotFlags) override
    {
        m_out.push_back(m_id);
    }

    void process_incoming(PeerState &, NetBuffer &, SnapshotFlags) override
    {
        m_in.push_back(m_id);
    }

private:
    int m_id;
    std::vector<int> &m_out;
    std::vector<int> &m_in;
};

// ==================================================================
// Test cases
// ==================================================================

TEST_CASE("NetPipeline: empty pipeline leaves buffer unchanged", "[pipeline]")
{
    NetPipeline pipeline;
    PeerState peer(0);
    NetBuffer buf;
    buf.write(static_cast<uint32_t>(42));
    pipeline.apply_outgoing(peer, buf, SnapshotFlags::FullSnapshot);
    buf.reset_data();
    REQUIRE(buf.read<uint32_t>() == 42);
}

TEST_CASE("NetPipeline: single processor roundtrip", "[pipeline]")
{
    NetPipeline pipeline;
    pipeline.add_processor(std::make_unique<AppendByteProcessor>(0xAB));
    PeerState peer(0);

    NetBuffer buf;
    buf.write(static_cast<uint32_t>(10));

    // Outgoing: original 4 bytes + 1 appended byte
    pipeline.apply_outgoing(peer, buf, SnapshotFlags::FullSnapshot);
    REQUIRE(buf.size() == 5);

    // Consume original data (advance read position past it)
    buf.reset_data();
    REQUIRE(buf.read<uint32_t>() == 10); // read position now at byte 4

    // Incoming: processor reads the appended byte
    pipeline.apply_incoming(peer, buf, SnapshotFlags::FullSnapshot);
    // Buffer should now be empty
    REQUIRE(buf.remaining() == 0);
}

TEST_CASE("NetPipeline: multiple processors call order", "[pipeline]")
{
    NetPipeline pipeline;
    std::vector<int> outOrder, inOrder;
    pipeline.add_processor(std::make_unique<CallOrderProcessor>(1, outOrder, inOrder));
    pipeline.add_processor(std::make_unique<CallOrderProcessor>(2, outOrder, inOrder));

    PeerState peer(0);
    NetBuffer buf;
    pipeline.apply_outgoing(peer, buf, SnapshotFlags::FullSnapshot);
    REQUIRE(outOrder == std::vector<int>{1, 2});

    pipeline.apply_incoming(peer, buf, SnapshotFlags::FullSnapshot);
    REQUIRE(inOrder == std::vector<int>{2, 1});
}
