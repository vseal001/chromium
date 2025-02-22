// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/network/p2p/socket_udp.h"

#include "base/bind.h"
#include "base/memory/ptr_util.h"
#include "base/metrics/histogram_macros.h"
#include "base/stl_util.h"
#include "base/strings/stringprintf.h"
#include "base/trace_event/trace_event.h"
#include "build/build_config.h"
#include "net/base/io_buffer.h"
#include "net/base/net_errors.h"
#include "net/log/net_log_source.h"
#include "services/network/p2p/socket_throttler.h"
#include "services/network/public/mojom/p2p.mojom.h"
#include "third_party/webrtc/media/base/rtputils.h"

namespace {

// UDP packets cannot be bigger than 64k.
const int kUdpReadBufferSize = 65536;
// Socket receive buffer size.
const int kUdpRecvSocketBufferSize = 65536;  // 64K
// Socket send buffer size.
const int kUdpSendSocketBufferSize = 65536;

// Defines set of transient errors. These errors are ignored when we get them
// from sendto() or recvfrom() calls.
//
// net::ERR_OUT_OF_MEMORY
//
// This is caused by ENOBUFS which means the buffer of the network interface
// is full.
//
// net::ERR_CONNECTION_RESET
//
// This is caused by WSAENETRESET or WSAECONNRESET which means the
// last send resulted in an "ICMP Port Unreachable" message.
struct {
  int code;
  const char* name;
} static const kTransientErrors[]{
    {net::ERR_ADDRESS_UNREACHABLE, "net::ERR_ADDRESS_UNREACHABLE"},
    {net::ERR_ADDRESS_INVALID, "net::ERR_ADDRESS_INVALID"},
    {net::ERR_ACCESS_DENIED, "net::ERR_ACCESS_DENIED"},
    {net::ERR_CONNECTION_RESET, "net::ERR_CONNECTION_RESET"},
    {net::ERR_OUT_OF_MEMORY, "net::ERR_OUT_OF_MEMORY"},
    {net::ERR_INTERNET_DISCONNECTED, "net::ERR_INTERNET_DISCONNECTED"}};

bool IsTransientError(int error) {
  for (const auto& transient_error : kTransientErrors) {
    if (transient_error.code == error)
      return true;
  }
  return false;
}

const char* GetTransientErrorName(int error) {
  for (const auto& transient_error : kTransientErrors) {
    if (transient_error.code == error)
      return transient_error.name;
  }
  return "";
}

}  // namespace

namespace network {

P2PSocketUdp::PendingPacket::PendingPacket(
    const net::IPEndPoint& to,
    const std::vector<int8_t>& content,
    const rtc::PacketOptions& options,
    uint64_t id,
    const net::NetworkTrafficAnnotationTag traffic_annotation)
    : to(to),
      data(new net::IOBuffer(content.size())),
      size(content.size()),
      packet_options(options),
      id(id),
      traffic_annotation(traffic_annotation) {
  memcpy(data->data(), &content[0], size);
}

P2PSocketUdp::PendingPacket::PendingPacket(const PendingPacket& other) =
    default;

P2PSocketUdp::PendingPacket::~PendingPacket() {}

P2PSocketUdp::P2PSocketUdp(P2PSocketManager* socket_manager,
                           mojom::P2PSocketClientPtr client,
                           mojom::P2PSocketRequest socket,
                           P2PMessageThrottler* throttler,
                           net::NetLog* net_log,
                           const DatagramServerSocketFactory& socket_factory)
    : P2PSocket(socket_manager,
                std::move(client),
                std::move(socket),
                P2PSocket::UDP),
      socket_(socket_factory.Run(net_log)),
      send_pending_(false),
      last_dscp_(net::DSCP_CS0),
      throttler_(throttler),
      net_log_(net_log),
      socket_factory_(socket_factory) {}

P2PSocketUdp::P2PSocketUdp(P2PSocketManager* socket_manager,
                           mojom::P2PSocketClientPtr client,
                           mojom::P2PSocketRequest socket,
                           P2PMessageThrottler* throttler,
                           net::NetLog* net_log)
    : P2PSocketUdp(socket_manager,
                   std::move(client),
                   std::move(socket),
                   throttler,
                   net_log,
                   base::Bind(&P2PSocketUdp::DefaultSocketFactory)) {}

P2PSocketUdp::~P2PSocketUdp() {
  if (state_ == STATE_OPEN) {
    DCHECK(socket_.get());
    socket_.reset();
  }
}

bool P2PSocketUdp::Init(const net::IPEndPoint& local_address,
                        uint16_t min_port,
                        uint16_t max_port,
                        const P2PHostAndIPEndPoint& remote_address) {
  DCHECK_EQ(state_, STATE_UNINITIALIZED);
  DCHECK((min_port == 0 && max_port == 0) || min_port > 0);
  DCHECK_LE(min_port, max_port);

  int result = -1;
  if (min_port == 0) {
    result = socket_->Listen(local_address);
  } else if (local_address.port() == 0) {
    for (unsigned port = min_port; port <= max_port && result < 0; ++port) {
      result = socket_->Listen(net::IPEndPoint(local_address.address(), port));
      if (result < 0 && port != max_port)
        socket_ = socket_factory_.Run(net_log_);
    }
  } else if (local_address.port() >= min_port &&
             local_address.port() <= max_port) {
    result = socket_->Listen(local_address);
  }
  if (result < 0) {
    LOG(ERROR) << "bind() to " << local_address.address().ToString()
               << (min_port == 0
                       ? base::StringPrintf(":%d", local_address.port())
                       : base::StringPrintf(", port range [%d-%d]", min_port,
                                            max_port))
               << " failed: " << result;
    OnError();
    return false;
  }

  // Setting recv socket buffer size.
  if (socket_->SetReceiveBufferSize(kUdpRecvSocketBufferSize) != net::OK) {
    LOG(WARNING) << "Failed to set socket receive buffer size to "
                 << kUdpRecvSocketBufferSize;
  }

  // Setting socket send buffer size.
  if (socket_->SetSendBufferSize(kUdpSendSocketBufferSize) != net::OK) {
    LOG(WARNING) << "Failed to set socket send buffer size to "
                 << kUdpSendSocketBufferSize;
  }

  net::IPEndPoint address;
  result = socket_->GetLocalAddress(&address);
  if (result < 0) {
    LOG(ERROR) << "P2PSocketUdp::Init(): unable to get local address: "
               << result;
    OnError();
    return false;
  }
  VLOG(1) << "Local address: " << address.ToString();

  state_ = STATE_OPEN;

  // NOTE: Remote address will be same as what renderer provided.
  client_->SocketCreated(address, remote_address.ip_address);

  recv_buffer_ = new net::IOBuffer(kUdpReadBufferSize);
  DoRead();

  return true;
}

void P2PSocketUdp::OnError() {
  socket_.reset();
  send_queue_.clear();

  if (state_ == STATE_UNINITIALIZED || state_ == STATE_OPEN) {
    binding_.Close();
    client_.reset();
  }

  state_ = STATE_ERROR;
}

void P2PSocketUdp::DoRead() {
  int result;
  do {
    result = socket_->RecvFrom(
        recv_buffer_.get(), kUdpReadBufferSize, &recv_address_,
        base::BindOnce(&P2PSocketUdp::OnRecv, base::Unretained(this)));
    if (result == net::ERR_IO_PENDING)
      return;
    HandleReadResult(result);
  } while (state_ == STATE_OPEN);
}

void P2PSocketUdp::OnRecv(int result) {
  HandleReadResult(result);
  if (state_ == STATE_OPEN) {
    DoRead();
  }
}

void P2PSocketUdp::HandleReadResult(int result) {
  DCHECK_EQ(STATE_OPEN, state_);

  if (result > 0) {
    std::vector<int8_t> data(recv_buffer_->data(),
                             recv_buffer_->data() + result);

    if (!base::ContainsKey(connected_peers_, recv_address_)) {
      P2PSocket::StunMessageType type;
      bool stun = GetStunPacketType(
          reinterpret_cast<const int8_t*>(&*data.begin()), data.size(), &type);
      if ((stun && IsRequestOrResponse(type))) {
        connected_peers_.insert(recv_address_);
      } else if (!stun || type == STUN_DATA_INDICATION) {
        LOG(ERROR) << "Received unexpected data packet from "
                   << recv_address_.ToString()
                   << " before STUN binding is finished.";
        return;
      }
    }

    client_->DataReceived(recv_address_, data, base::TimeTicks::Now());

    if (dump_incoming_rtp_packet_)
      DumpRtpPacket(&data[0], data.size(), true);
  } else if (result < 0 && !IsTransientError(result)) {
    LOG(ERROR) << "Error when reading from UDP socket: " << result;
    OnError();
  }
}

void P2PSocketUdp::DoSend(const PendingPacket& packet) {
  base::TimeTicks send_time = base::TimeTicks::Now();

  // The peer is considered not connected until the first incoming STUN
  // request/response. In that state the renderer is allowed to send only STUN
  // messages to that peer and they are throttled using the |throttler_|. This
  // has to be done here instead of Send() to ensure P2PMsg_OnSendComplete
  // messages are sent in correct order.
  if (!base::ContainsKey(connected_peers_, packet.to)) {
    P2PSocket::StunMessageType type = P2PSocket::StunMessageType();
    bool stun =
        GetStunPacketType(reinterpret_cast<const int8_t*>(packet.data->data()),
                          packet.size, &type);
    if (!stun || type == STUN_DATA_INDICATION) {
      LOG(ERROR) << "Page tried to send a data packet to "
                 << packet.to.ToString() << " before STUN binding is finished.";
      OnError();
      return;
    }

    if (throttler_->DropNextPacket(packet.size)) {
      VLOG(0) << "Throttling outgoing STUN message.";
      // The renderer expects P2PMsg_OnSendComplete for all packets it generates
      // and in the same order it generates them, so we need to respond even
      // when the packet is dropped.
      client_->SendComplete(P2PSendPacketMetrics(
          packet.id, packet.packet_options.packet_id, send_time));
      // Do not reset the socket.
      return;
    }
  }

  TRACE_EVENT_ASYNC_STEP_INTO1("p2p", "Send", packet.id, "UdpAsyncSendTo",
                               "size", packet.size);
  // Don't try to set DSCP in following conditions,
  // 1. If the outgoing packet is set to DSCP_NO_CHANGE
  // 2. If no change in DSCP value from last packet
  // 3. If there is any error in setting DSCP on socket.
  net::DiffServCodePoint dscp =
      static_cast<net::DiffServCodePoint>(packet.packet_options.dscp);
  if (dscp != net::DSCP_NO_CHANGE && last_dscp_ != dscp &&
      last_dscp_ != net::DSCP_NO_CHANGE) {
    int result = SetSocketDiffServCodePointInternal(dscp);
    if (result == net::OK) {
      last_dscp_ = dscp;
    } else if (!IsTransientError(result) && last_dscp_ != net::DSCP_CS0) {
      // We receieved a non-transient error, and it seems we have
      // not changed the DSCP in the past, disable DSCP as it unlikely
      // to work in the future.
      last_dscp_ = net::DSCP_NO_CHANGE;
    }
  }

  cricket::ApplyPacketOptions(reinterpret_cast<uint8_t*>(packet.data->data()),
                              packet.size,
                              packet.packet_options.packet_time_params,
                              (send_time - base::TimeTicks()).InMicroseconds());
  auto callback_binding =
      base::Bind(&P2PSocketUdp::OnSend, base::Unretained(this), packet.id,
                 packet.packet_options.packet_id, send_time);

  // TODO(crbug.com/656607): Pass traffic annotation after DatagramSocketServer
  // is updated.
  int result = socket_->SendTo(packet.data.get(), packet.size, packet.to,
                               callback_binding);

  // sendto() may return an error, e.g. if we've received an ICMP Destination
  // Unreachable message. When this happens try sending the same packet again,
  // and just drop it if it fails again.
  if (IsTransientError(result)) {
    result = socket_->SendTo(packet.data.get(), packet.size, packet.to,
                             std::move(callback_binding));
  }

  if (result == net::ERR_IO_PENDING) {
    send_pending_ = true;
  } else {
    HandleSendResult(packet.id, packet.packet_options.packet_id, send_time,
                     result);
  }

  if (dump_outgoing_rtp_packet_)
    DumpRtpPacket(reinterpret_cast<const int8_t*>(packet.data->data()),
                  packet.size, false);
}

void P2PSocketUdp::OnSend(uint64_t packet_id,
                          int32_t transport_sequence_number,
                          base::TimeTicks send_time,
                          int result) {
  DCHECK(send_pending_);
  DCHECK_NE(result, net::ERR_IO_PENDING);

  send_pending_ = false;

  HandleSendResult(packet_id, transport_sequence_number, send_time, result);

  // Send next packets if we have them waiting in the buffer.
  while (state_ == STATE_OPEN && !send_queue_.empty() && !send_pending_) {
    PendingPacket packet = send_queue_.front();
    send_queue_.pop_front();
    DoSend(packet);
    DecrementDelayedBytes(packet.size);
  }
}

void P2PSocketUdp::HandleSendResult(uint64_t packet_id,
                                    int32_t transport_sequence_number,
                                    base::TimeTicks send_time,
                                    int result) {
  TRACE_EVENT_ASYNC_END1("p2p", "Send", packet_id, "result", result);
  if (result < 0) {
    ReportSocketError(result, "WebRTC.ICE.UdpSocketWriteErrorCode");

    if (!IsTransientError(result)) {
      LOG(ERROR) << "Error when sending data in UDP socket: " << result;
      OnError();
      return;
    }
    VLOG(0) << "sendto() has failed twice returning a "
               " transient error "
            << GetTransientErrorName(result) << ". Dropping the packet.";
  }

  // UMA to track the histograms from 1ms to 1 sec for how long a packet spends
  // in the browser process.
  UMA_HISTOGRAM_TIMES("WebRTC.SystemSendPacketDuration_UDP" /* name */,
                      base::TimeTicks::Now() - send_time /* sample */);

  client_->SendComplete(
      P2PSendPacketMetrics(packet_id, transport_sequence_number, send_time));
}

void P2PSocketUdp::AcceptIncomingTcpConnection(
    const net::IPEndPoint& remote_address,
    mojom::P2PSocketClientPtr client,
    mojom::P2PSocketRequest socket) {
  NOTREACHED();
  OnError();
}

void P2PSocketUdp::Send(
    const std::vector<int8_t>& data,
    const P2PPacketInfo& packet_info,
    const net::MutableNetworkTrafficAnnotationTag& traffic_annotation) {
  if (data.size() > kMaximumPacketSize) {
    LOG(ERROR) << "Received P2PHostMsg_Send with a packet that is too big: "
               << data.size();
    delete this;
  }

  if (!socket_) {
    // The Send message may be sent after the an OnError message was
    // sent by hasn't been processed the renderer.
    return;
  }

  IncrementTotalSentPackets();

  if (send_pending_) {
    send_queue_.push_back(
        PendingPacket(packet_info.destination, data, packet_info.packet_options,
                      packet_info.packet_id,
                      net::NetworkTrafficAnnotationTag(traffic_annotation)));
    IncrementDelayedBytes(data.size());
    IncrementDelayedPackets();
  } else {
    PendingPacket packet(packet_info.destination, data,
                         packet_info.packet_options, packet_info.packet_id,
                         net::NetworkTrafficAnnotationTag(traffic_annotation));
    DoSend(packet);
  }
}

void P2PSocketUdp::SetOption(P2PSocketOption option, int32_t value) {
  if (state_ != STATE_OPEN) {
    DCHECK_EQ(state_, STATE_ERROR);
    return;
  }
  switch (option) {
    case P2P_SOCKET_OPT_RCVBUF:
      socket_->SetReceiveBufferSize(value);
      break;
    case P2P_SOCKET_OPT_SNDBUF:
      socket_->SetSendBufferSize(value);
      break;
    case P2P_SOCKET_OPT_DSCP:
      SetSocketDiffServCodePointInternal(
          static_cast<net::DiffServCodePoint>(value));
      break;
    default:
      NOTREACHED();
  }
}

// TODO(crbug.com/812137): We don't call SetDiffServCodePoint for the Windows
// UDP socket, because this is known to cause a hanging thread.
int P2PSocketUdp::SetSocketDiffServCodePointInternal(
    net::DiffServCodePoint dscp) {
#if defined(OS_WIN)
  return net::OK;
#else
  return socket_->SetDiffServCodePoint(dscp);
#endif
}

// static
std::unique_ptr<net::DatagramServerSocket> P2PSocketUdp::DefaultSocketFactory(
    net::NetLog* net_log) {
  net::UDPServerSocket* socket =
      new net::UDPServerSocket(net_log, net::NetLogSource());
#if defined(OS_WIN)
  socket->UseNonBlockingIO();
#endif

  return base::WrapUnique(socket);
}

}  // namespace network
