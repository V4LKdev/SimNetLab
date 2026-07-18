#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <variant>
#include <vector>

import simnet.core;
import simnet.transport;

namespace {
constexpr auto poll_timeout = std::chrono::seconds(2);

[[nodiscard]] std::uint64_t smoke_token() {
  static auto const token = static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
  return token;
}

[[nodiscard]] std::uint16_t smoke_port() { return static_cast<std::uint16_t>(20000U + smoke_token() % 20000U); }

[[nodiscard]] simnet::SessionIdentity identity(std::uint32_t protocol = 1) {
  return {
      .application_protocol_version = protocol,
      .compatibility_fingerprint = 0x1234U,
      .pipeline_decode_signature = 0x5678U,
      .capabilities = 0U,
  };
}

struct HandshakeResult {
  bool server_ready{};
  bool client_ready{};
  bool rejected{};
  simnet::PeerId server_peer{};
};

[[nodiscard]] bool poll_once(simnet::TransportServer &server, simnet::TransportClient &client,
                             HandshakeResult &result) {
  auto events = std::vector<simnet::TransportEvent>{};
  auto poll = server.poll(events, 0);
  if (!poll.ok) {
    std::cerr << "server poll failed: " << poll.error.message << '\n';
    return false;
  }
  for (auto const &event : events) {
    if (auto const *ready = std::get_if<simnet::PeerSessionReady>(&event)) {
      result.server_ready = true;
      result.server_peer = ready->peer;
    } else if (std::holds_alternative<simnet::PeerDisconnected>(event) ||
               std::holds_alternative<simnet::TransportErrorEvent>(event)) {
      result.rejected = true;
    }
  }

  events.clear();
  poll = client.poll(events, 1);
  if (!poll.ok) {
    std::cerr << "client poll failed: " << poll.error.message << '\n';
    return false;
  }
  for (auto const &event : events) {
    if (std::holds_alternative<simnet::PeerSessionReady>(event)) {
      result.client_ready = true;
    } else if (std::holds_alternative<simnet::PeerDisconnected>(event) ||
               std::holds_alternative<simnet::TransportErrorEvent>(event)) {
      result.rejected = true;
    }
  }
  return true;
}

[[nodiscard]] bool poll_until(simnet::TransportServer &server, simnet::TransportClient &client, HandshakeResult &result,
                              bool want_ready) {
  auto const deadline = std::chrono::steady_clock::now() + poll_timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (!poll_once(server, client, result)) {
      return false;
    }
    if (want_ready && result.server_ready && result.client_ready) {
      return true;
    }
    if (!want_ready && result.rejected) {
      return true;
    }
  }
  return false;
}

struct SmokeSettings {
  std::uint16_t port{smoke_port()};
};

[[nodiscard]] simnet::TransportServerSettings server_settings(SmokeSettings const &settings,
                                                              simnet::SessionIdentity expected_identity = identity()) {
  return {
      .bind_address = "127.0.0.1",
      .port = settings.port,
      .max_peers = 1U,
      .expected_identity = expected_identity,
  };
}

[[nodiscard]] simnet::TransportClientSettings client_settings(SmokeSettings const &settings,
                                                              simnet::SessionIdentity client_identity = identity()) {
  return {
      .server_address = "127.0.0.1",
      .server_port = settings.port,
      .identity = client_identity,
  };
}

[[nodiscard]] bool matching_session_smoke(SmokeSettings const &settings) {
  auto server = simnet::TransportServer{};
  auto client = simnet::TransportClient{};
  auto limits = simnet::TransportLimits{
      .max_payload_bytes = 64U,
      .size_policy = simnet::SendSizePolicy::EnforceLimit,
  };

  auto server_config = server_settings(settings);
  server_config.limits = limits;
  auto result = server.start(server_config);
  if (!result.ok) {
    std::cerr << "server start failed: " << result.error.message << '\n';
    return false;
  }

  auto client_config = client_settings(settings);
  client_config.limits = limits;
  result = client.connect(client_config);
  if (!result.ok) {
    std::cerr << "client connect failed: " << result.error.message << '\n';
    return false;
  }

  auto handshake = HandshakeResult{};
  if (!poll_until(server, client, handshake, true)) {
    std::cerr << "matching session did not become ready\n";
    return false;
  }

  auto payload = std::array<simnet::Byte, 128>{};
  result = server.send({
      .peer = handshake.server_peer,
      .lane = simnet::Lane::Snapshot,
      .delivery = simnet::Delivery::ReliableSequenced,
      .payload = payload,
  });
  if (result.ok || result.error.code != simnet::TransportErrorCode::PayloadTooLarge) {
    std::cerr << "oversize send did not fail with PayloadTooLarge\n";
    return false;
  }

  auto small_payload = std::array<simnet::Byte, 4>{};
  result = server.send({
      .peer = handshake.server_peer,
      .lane = simnet::Lane::Control,
      .delivery = simnet::Delivery::ReliableSequenced,
      .payload = small_payload,
  });
  if (result.ok || result.error.code != simnet::TransportErrorCode::InvalidLane) {
    std::cerr << "server reserved Control lane send was not rejected\n";
    return false;
  }
  result = client.send(simnet::Lane::Control, simnet::Delivery::ReliableSequenced, small_payload);
  if (result.ok || result.error.code != simnet::TransportErrorCode::InvalidLane) {
    std::cerr << "client reserved Control lane send was not rejected\n";
    return false;
  }
  result = client.send(simnet::Lane::Input, simnet::Delivery::ReliableSequenced, small_payload);
  if (result.ok || result.error.code != simnet::TransportErrorCode::InvalidLane) {
    std::cerr << "client reserved Input lane send was not rejected\n";
    return false;
  }
  result = client.send(static_cast<simnet::Lane>(255U), simnet::Delivery::ReliableSequenced, small_payload);
  if (result.ok || result.error.code != simnet::TransportErrorCode::InvalidLane) {
    std::cerr << "invalid lane send was not rejected\n";
    return false;
  }
  result = client.send(simnet::Lane::Snapshot, static_cast<simnet::Delivery>(255U), small_payload);
  if (result.ok || result.error.code != simnet::TransportErrorCode::InvalidDelivery) {
    std::cerr << "invalid delivery send was not rejected\n";
    return false;
  }

  auto const expected_ack = simnet::SnapshotAck{
      .newest_received_snapshot = 7U,
      .received_mask = 0x35U,
      .newest_applied_snapshot = 6U,
  };
  auto const ack_sent = client.send_snapshot_ack(expected_ack);
  if (!ack_sent.ok) {
    std::cerr << "snapshot ACK send failed: " << ack_sent.error.message << '\n';
    return false;
  }
  auto ack_seen = false;
  auto const ack_deadline = std::chrono::steady_clock::now() + poll_timeout;
  while (!ack_seen && std::chrono::steady_clock::now() < ack_deadline) {
    auto events = std::vector<simnet::TransportEvent>{};
    auto poll = client.poll(events, 0);
    if (!poll.ok) {
      std::cerr << "client ACK service poll failed: " << poll.error.message << '\n';
      return false;
    }
    events.clear();
    poll = server.poll(events, 10);
    if (!poll.ok) {
      std::cerr << "server ACK poll failed: " << poll.error.message << '\n';
      return false;
    }
    for (auto const &event : events) {
      if (auto const *ack = std::get_if<simnet::SnapshotAckReceived>(&event)) {
        ack_seen = ack->ack.newest_received_snapshot == expected_ack.newest_received_snapshot &&
                   ack->ack.received_mask == expected_ack.received_mask &&
                   ack->ack.newest_applied_snapshot == expected_ack.newest_applied_snapshot;
      }
    }
  }
  if (!ack_seen) {
    std::cerr << "snapshot ACK was not received intact\n";
    return false;
  }

  client.disconnect(simnet::DisconnectCode::None);
  server.stop();
  return true;
}

[[nodiscard]] bool mismatched_session_smoke(SmokeSettings const &settings,
                                            simnet::SessionIdentity mismatched_identity = identity(2U)) {
  auto server = simnet::TransportServer{};
  auto client = simnet::TransportClient{};

  auto result = server.start(server_settings(settings));
  if (!result.ok) {
    std::cerr << "server start failed: " << result.error.message << '\n';
    return false;
  }

  result = client.connect(client_settings(settings, mismatched_identity));
  if (!result.ok) {
    std::cerr << "client connect failed: " << result.error.message << '\n';
    return false;
  }

  auto handshake = HandshakeResult{};
  if (!poll_until(server, client, handshake, false)) {
    std::cerr << "mismatched session did not reject\n";
    return false;
  }
  if (handshake.server_ready || handshake.client_ready) {
    std::cerr << "mismatched session became ready\n";
    return false;
  }

  client.disconnect(simnet::DisconnectCode::None);
  server.stop();
  return true;
}

[[nodiscard]] bool reconnect_smoke(SmokeSettings const &settings) {
  auto server = simnet::TransportServer{};
  auto client = simnet::TransportClient{};
  auto result = server.start(server_settings(settings));
  if (!result.ok) {
    std::cerr << "reconnect server start failed: " << result.error.message << '\n';
    return false;
  }

  result = client.connect(client_settings(settings));
  if (!result.ok) {
    std::cerr << "initial reconnect client connect failed: " << result.error.message << '\n';
    return false;
  }
  auto first = HandshakeResult{};
  if (!poll_until(server, client, first, true)) {
    std::cerr << "initial reconnect session did not become ready\n";
    return false;
  }

  client.disconnect(simnet::DisconnectCode::None);
  auto disconnected = false;
  auto const disconnect_deadline = std::chrono::steady_clock::now() + poll_timeout;
  while (!disconnected && std::chrono::steady_clock::now() < disconnect_deadline) {
    auto events = std::vector<simnet::TransportEvent>{};
    auto const poll = server.poll(events, 10);
    if (!poll.ok) {
      std::cerr << "reconnect disconnect poll failed: " << poll.error.message << '\n';
      return false;
    }
    disconnected = std::ranges::any_of(events, [](simnet::TransportEvent const &event) {
      return std::holds_alternative<simnet::PeerDisconnected>(event);
    });
  }
  if (!disconnected) {
    std::cerr << "server did not observe reconnect disconnect\n";
    return false;
  }

  result = client.connect(client_settings(settings));
  if (!result.ok) {
    std::cerr << "second reconnect client connect failed: " << result.error.message << '\n';
    return false;
  }
  auto second = HandshakeResult{};
  if (!poll_until(server, client, second, true)) {
    std::cerr << "second reconnect session did not become ready\n";
    return false;
  }

  client.disconnect(simnet::DisconnectCode::None);
  server.stop();
  return true;
}

[[nodiscard]] bool receive_limit_smoke(SmokeSettings const &settings) {
  auto server = simnet::TransportServer{};
  auto client = simnet::TransportClient{};
  auto server_config = server_settings(settings);
  server_config.limits = {
      .max_payload_bytes = 64U,
      .size_policy = simnet::SendSizePolicy::EnforceLimit,
  };
  auto result = server.start(server_config);
  if (!result.ok) {
    std::cerr << "receive-limit server start failed: " << result.error.message << '\n';
    return false;
  }

  auto client_config = client_settings(settings);
  client_config.limits = {
      .max_payload_bytes = 64U,
      .size_policy = simnet::SendSizePolicy::AllowBackendFragmentation,
  };
  result = client.connect(client_config);
  if (!result.ok) {
    std::cerr << "receive-limit client connect failed: " << result.error.message << '\n';
    return false;
  }
  auto handshake = HandshakeResult{};
  if (!poll_until(server, client, handshake, true)) {
    std::cerr << "receive-limit session did not become ready\n";
    return false;
  }

  auto payload = std::array<simnet::Byte, 128>{};
  result = client.send(simnet::Lane::Snapshot, simnet::Delivery::ReliableSequenced, payload);
  if (!result.ok) {
    std::cerr << "receive-limit sender unexpectedly rejected payload: " << result.error.message << '\n';
    return false;
  }

  auto rejected = false;
  auto const deadline = std::chrono::steady_clock::now() + poll_timeout;
  while (!rejected && std::chrono::steady_clock::now() < deadline) {
    auto events = std::vector<simnet::TransportEvent>{};
    auto poll = client.poll(events, 0);
    if (!poll.ok) {
      std::cerr << "receive-limit client service failed: " << poll.error.message << '\n';
      return false;
    }
    events.clear();
    poll = server.poll(events, 10);
    if (!poll.ok) {
      rejected = poll.error.code == simnet::TransportErrorCode::PayloadTooLarge;
    } else {
      for (auto const &event : events) {
        if (std::holds_alternative<simnet::ReceivedPacket>(event)) {
          std::cerr << "oversized receive escaped transport limit\n";
          return false;
        }
        rejected = rejected || std::holds_alternative<simnet::TransportErrorEvent>(event) ||
                   std::holds_alternative<simnet::PeerDisconnected>(event);
      }
    }
  }
  if (!rejected || server.stats().oversize_drops == 0) {
    std::cerr << "oversized receive was not rejected and counted\n";
    return false;
  }

  client.disconnect(simnet::DisconnectCode::None);
  server.stop();
  return true;
}

} // namespace

TEST_CASE("ENet session handshake and transport contract", "[transport][enet][integration]") {
  auto const enet = SmokeSettings{};
  REQUIRE(matching_session_smoke(enet));
}

TEST_CASE("ENet rejects incompatible session identities", "[transport][enet][integration]") {
  auto const enet = SmokeSettings{};
  REQUIRE(mismatched_session_smoke(enet));

  auto fingerprint_mismatch = identity();
  fingerprint_mismatch.compatibility_fingerprint ^= 1U;
  REQUIRE(mismatched_session_smoke(enet, fingerprint_mismatch));

  auto signature_mismatch = identity();
  signature_mismatch.pipeline_decode_signature ^= 1U;
  REQUIRE(mismatched_session_smoke(enet, signature_mismatch));
}

TEST_CASE("ENet disconnects and reconnects", "[transport][enet][integration]") {
  REQUIRE(reconnect_smoke(SmokeSettings{}));
}

TEST_CASE("ENet enforces receive limits", "[transport][enet][integration]") {
  REQUIRE(receive_limit_smoke(SmokeSettings{}));
}
