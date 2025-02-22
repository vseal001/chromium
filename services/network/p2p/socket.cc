// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/network/p2p/socket.h"

#include "base/metrics/histogram_macros.h"
#include "base/sys_byteorder.h"
#include "net/base/net_errors.h"
#include "net/url_request/url_request_context.h"
#include "net/url_request/url_request_context_getter.h"
#include "services/network/p2p/socket_manager.h"
#include "services/network/p2p/socket_tcp.h"
#include "services/network/p2p/socket_tcp_server.h"
#include "services/network/p2p/socket_udp.h"
#include "services/network/proxy_resolving_client_socket_factory.h"
#include "third_party/webrtc/media/base/rtputils.h"
#include "third_party/webrtc/media/base/turnutils.h"

namespace {

// Used to back histogram value of "WebRTC.ICE.TcpSocketErrorCode" and
// "WebRTC.ICE.UdpSocketErrorCode".
enum class SocketErrorCode {
  ERR_MSG_TOO_BIG,
  ERR_ADDRESS_UNREACHABLE,
  ERR_ADDRESS_INVALID,
  ERR_INTERNET_DISCONNECTED,
  ERR_TIMED_OUT,
  ERR_INSUFFICIENT_RESOURCES,
  ERR_OUT_OF_MEMORY,
  ERR_OTHER  // For all the others
};

const uint32_t kStunMagicCookie = 0x2112A442;
const size_t kMinRtcpHeaderLength = 8;
const size_t kDtlsRecordHeaderLength = 13;

bool IsDtlsPacket(const int8_t* data, size_t length) {
  const uint8_t* u = reinterpret_cast<const uint8_t*>(data);
  return (length >= kDtlsRecordHeaderLength && (u[0] > 19 && u[0] < 64));
}

bool IsRtcpPacket(const int8_t* data, size_t length) {
  if (length < kMinRtcpHeaderLength) {
    return false;
  }

  int type = (static_cast<uint8_t>(data[1]) & 0x7F);
  return (type >= 64 && type < 96);
}

// Map the network error to SocketErrorCode for the UMA histogram.
// static
static SocketErrorCode MapNetErrorToSocketErrorCode(int net_err) {
  switch (net_err) {
    case net::OK:
      NOTREACHED();
      return SocketErrorCode::ERR_OTHER;
    case net::ERR_MSG_TOO_BIG:
      return SocketErrorCode::ERR_MSG_TOO_BIG;
    case net::ERR_ADDRESS_UNREACHABLE:
      return SocketErrorCode::ERR_ADDRESS_UNREACHABLE;
    case net::ERR_ADDRESS_INVALID:
      return SocketErrorCode::ERR_ADDRESS_INVALID;
    case net::ERR_INTERNET_DISCONNECTED:
      return SocketErrorCode::ERR_INTERNET_DISCONNECTED;
    case net::ERR_TIMED_OUT:
      return SocketErrorCode::ERR_TIMED_OUT;
    case net::ERR_INSUFFICIENT_RESOURCES:
      return SocketErrorCode::ERR_INSUFFICIENT_RESOURCES;
    case net::ERR_OUT_OF_MEMORY:
      return SocketErrorCode::ERR_OUT_OF_MEMORY;
    default:
      return SocketErrorCode::ERR_OTHER;
  }
}
}  // namespace

namespace network {

P2PSocket::P2PSocket(P2PSocketManager* socket_manager,
                     mojom::P2PSocketClientPtr client,
                     mojom::P2PSocketRequest socket,
                     ProtocolType protocol_type)
    : socket_manager_(socket_manager),
      client_(std::move(client)),
      binding_(this, std::move(socket)),
      state_(STATE_UNINITIALIZED),
      dump_incoming_rtp_packet_(false),
      dump_outgoing_rtp_packet_(false),
      protocol_type_(protocol_type),
      send_packets_delayed_total_(0),
      send_packets_total_(0),
      send_bytes_delayed_max_(0),
      send_bytes_delayed_cur_(0),
      weak_ptr_factory_(this) {
  binding_.set_connection_error_handler(
      base::BindOnce(&P2PSocket::OnConnectionError, base::Unretained(this)));
}

P2PSocket::~P2PSocket() {
  if (protocol_type_ == P2PSocket::UDP) {
    UMA_HISTOGRAM_COUNTS_10000("WebRTC.SystemMaxConsecutiveBytesDelayed_UDP",
                               send_bytes_delayed_max_);
  } else {
    UMA_HISTOGRAM_COUNTS_10000("WebRTC.SystemMaxConsecutiveBytesDelayed_TCP",
                               send_bytes_delayed_max_);
  }

  if (send_packets_total_ > 0) {
    int delay_rate = (send_packets_delayed_total_ * 100) / send_packets_total_;
    if (protocol_type_ == P2PSocket::UDP) {
      UMA_HISTOGRAM_PERCENTAGE("WebRTC.SystemPercentPacketsDelayed_UDP",
                               delay_rate);
    } else {
      UMA_HISTOGRAM_PERCENTAGE("WebRTC.SystemPercentPacketsDelayed_TCP",
                               delay_rate);
    }
  }
}

// Verifies that the packet |data| has a valid STUN header.
// static
bool P2PSocket::GetStunPacketType(const int8_t* data,
                                  int data_size,
                                  StunMessageType* type) {
  if (data_size < kStunHeaderSize) {
    return false;
  }

  uint32_t cookie =
      base::NetToHost32(*reinterpret_cast<const uint32_t*>(data + 4));
  if (cookie != kStunMagicCookie) {
    return false;
  }

  uint16_t length =
      base::NetToHost16(*reinterpret_cast<const uint16_t*>(data + 2));
  if (length != data_size - kStunHeaderSize) {
    return false;
  }

  int message_type =
      base::NetToHost16(*reinterpret_cast<const uint16_t*>(data));

  // Verify that the type is known:
  switch (message_type) {
    case STUN_BINDING_REQUEST:
    case STUN_BINDING_RESPONSE:
    case STUN_BINDING_ERROR_RESPONSE:
    case STUN_SHARED_SECRET_REQUEST:
    case STUN_SHARED_SECRET_RESPONSE:
    case STUN_SHARED_SECRET_ERROR_RESPONSE:
    case STUN_ALLOCATE_REQUEST:
    case STUN_ALLOCATE_RESPONSE:
    case STUN_ALLOCATE_ERROR_RESPONSE:
    case STUN_SEND_REQUEST:
    case STUN_SEND_RESPONSE:
    case STUN_SEND_ERROR_RESPONSE:
    case STUN_DATA_INDICATION:
      *type = static_cast<StunMessageType>(message_type);
      return true;

    default:
      return false;
  }
}

// static
bool P2PSocket::IsRequestOrResponse(StunMessageType type) {
  return type == STUN_BINDING_REQUEST || type == STUN_BINDING_RESPONSE ||
         type == STUN_ALLOCATE_REQUEST || type == STUN_ALLOCATE_RESPONSE;
}

// static
void P2PSocket::ReportSocketError(int result, const char* histogram_name) {
  SocketErrorCode error_code = MapNetErrorToSocketErrorCode(result);
  UMA_HISTOGRAM_ENUMERATION(histogram_name, static_cast<int>(error_code),
                            static_cast<int>(SocketErrorCode::ERR_OTHER) + 1);
}

// static
P2PSocket* P2PSocket::Create(
    P2PSocketManager* socket_manager,
    mojom::P2PSocketClientPtr client,
    mojom::P2PSocketRequest socket,
    P2PSocketType type,
    net::NetLog* net_log,
    ProxyResolvingClientSocketFactory* proxy_resolving_socket_factory,
    P2PMessageThrottler* throttler) {
  switch (type) {
    case P2P_SOCKET_UDP:
      return new P2PSocketUdp(socket_manager, std::move(client),
                              std::move(socket), throttler, net_log);
    case P2P_SOCKET_TCP_SERVER:
      return new P2PSocketTcpServer(socket_manager, std::move(client),
                                    std::move(socket), P2P_SOCKET_TCP_CLIENT);

    case P2P_SOCKET_STUN_TCP_SERVER:
      return new P2PSocketTcpServer(socket_manager, std::move(client),
                                    std::move(socket),
                                    P2P_SOCKET_STUN_TCP_CLIENT);

    case P2P_SOCKET_TCP_CLIENT:
    case P2P_SOCKET_SSLTCP_CLIENT:
    case P2P_SOCKET_TLS_CLIENT:
      return new P2PSocketTcp(socket_manager, std::move(client),
                              std::move(socket), type,
                              proxy_resolving_socket_factory);

    case P2P_SOCKET_STUN_TCP_CLIENT:
    case P2P_SOCKET_STUN_SSLTCP_CLIENT:
    case P2P_SOCKET_STUN_TLS_CLIENT:
      return new P2PSocketStunTcp(socket_manager, std::move(client),
                                  std::move(socket), type,
                                  proxy_resolving_socket_factory);
  }

  NOTREACHED();
  return nullptr;
}

void P2PSocket::StartRtpDump(bool incoming, bool outgoing) {
  DCHECK(incoming || outgoing);

  if (incoming) {
    dump_incoming_rtp_packet_ = true;
  }

  if (outgoing) {
    dump_outgoing_rtp_packet_ = true;
  }
}

void P2PSocket::StopRtpDump(bool incoming, bool outgoing) {
  DCHECK(incoming || outgoing);

  if (incoming) {
    dump_incoming_rtp_packet_ = false;
  }

  if (outgoing) {
    dump_outgoing_rtp_packet_ = false;
  }
}

mojom::P2PSocketClientPtr P2PSocket::ReleaseClientForTesting() {
  return std::move(client_);
}

mojom::P2PSocketRequest P2PSocket::ReleaseBindingForTesting() {
  return binding_.Unbind();
}

void P2PSocket::DumpRtpPacket(const int8_t* packet,
                              size_t length,
                              bool incoming) {
  if (!socket_manager_ || IsDtlsPacket(packet, length) ||
      IsRtcpPacket(packet, length)) {
    return;
  }

  size_t rtp_packet_pos = 0;
  size_t rtp_packet_length = length;
  if (!cricket::UnwrapTurnPacket(reinterpret_cast<const uint8_t*>(packet),
                                 length, &rtp_packet_pos, &rtp_packet_length)) {
    return;
  }

  packet += rtp_packet_pos;

  size_t header_length = 0;
  bool valid =
      cricket::ValidateRtpHeader(reinterpret_cast<const uint8_t*>(packet),
                                 rtp_packet_length, &header_length);
  if (!valid) {
    NOTREACHED();
    return;
  }

  socket_manager_->DumpPacket(packet, header_length, rtp_packet_length,
                              incoming);
}

void P2PSocket::IncrementDelayedPackets() {
  send_packets_delayed_total_++;
}

void P2PSocket::IncrementTotalSentPackets() {
  send_packets_total_++;
}

void P2PSocket::IncrementDelayedBytes(uint32_t size) {
  send_bytes_delayed_cur_ += size;
  if (send_bytes_delayed_cur_ > send_bytes_delayed_max_) {
    send_bytes_delayed_max_ = send_bytes_delayed_cur_;
  }
}

void P2PSocket::DecrementDelayedBytes(uint32_t size) {
  send_bytes_delayed_cur_ -= size;
  DCHECK_GE(send_bytes_delayed_cur_, 0);
}

void P2PSocket::OnConnectionError() {
  if (socket_manager_)
    socket_manager_->DestroySocket(this);
}

}  // namespace network
