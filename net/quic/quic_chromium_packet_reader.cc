// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/quic_chromium_packet_reader.h"

#include "base/location.h"
#include "base/metrics/histogram_macros.h"
#include "base/single_thread_task_runner.h"
#include "base/threading/thread_task_runner_handle.h"
#include "net/base/net_errors.h"
#include "net/third_party/quic/platform/api/quic_clock.h"

namespace net {

QuicChromiumPacketReader::QuicChromiumPacketReader(
    DatagramClientSocket* socket,
    quic::QuicClock* clock,
    Visitor* visitor,
    int yield_after_packets,
    quic::QuicTime::Delta yield_after_duration,
    const NetLogWithSource& net_log)
    : socket_(socket),
      visitor_(visitor),
      read_pending_(false),
      num_packets_read_(0),
      clock_(clock),
      yield_after_packets_(yield_after_packets),
      yield_after_duration_(yield_after_duration),
      yield_after_(quic::QuicTime::Infinite()),
      read_buffer_(
          new IOBufferWithSize(static_cast<size_t>(quic::kMaxPacketSize))),
      net_log_(net_log),
      weak_factory_(this) {}

QuicChromiumPacketReader::~QuicChromiumPacketReader() {}

void QuicChromiumPacketReader::StartReading() {
  for (;;) {
    if (read_pending_)
      return;

    if (num_packets_read_ == 0)
      yield_after_ = clock_->Now() + yield_after_duration_;

    DCHECK(socket_);
    read_pending_ = true;
    int rv =
        socket_->Read(read_buffer_.get(), read_buffer_->size(),
                      base::BindOnce(&QuicChromiumPacketReader::OnReadComplete,
                                     weak_factory_.GetWeakPtr()));
    UMA_HISTOGRAM_BOOLEAN("Net.QuicSession.AsyncRead", rv == ERR_IO_PENDING);
    if (rv == ERR_IO_PENDING) {
      num_packets_read_ = 0;
      return;
    }

    if (++num_packets_read_ > yield_after_packets_ ||
        clock_->Now() > yield_after_) {
      num_packets_read_ = 0;
      // Data was read, process it.
      // Schedule the work through the message loop to 1) prevent infinite
      // recursion and 2) avoid blocking the thread for too long.
      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::Bind(&QuicChromiumPacketReader::OnReadComplete,
                                weak_factory_.GetWeakPtr(), rv));
    } else {
      if (!ProcessReadResult(rv)) {
        return;
      }
    }
  }
}

size_t QuicChromiumPacketReader::EstimateMemoryUsage() const {
  // Return the size of |read_buffer_|.
  return quic::kMaxPacketSize;
}

bool QuicChromiumPacketReader::ProcessReadResult(int result) {
  read_pending_ = false;
  if (result == 0)
    result = ERR_CONNECTION_CLOSED;

  if (result < 0) {
    visitor_->OnReadError(result, socket_);
    return false;
  }

  quic::QuicReceivedPacket packet(read_buffer_->data(), result, clock_->Now());
  IPEndPoint local_address;
  IPEndPoint peer_address;
  socket_->GetLocalAddress(&local_address);
  socket_->GetPeerAddress(&peer_address);
  return visitor_->OnPacket(
      packet,
      quic::QuicSocketAddress(quic::QuicSocketAddressImpl(local_address)),
      quic::QuicSocketAddress(quic::QuicSocketAddressImpl(peer_address)));
}

void QuicChromiumPacketReader::OnReadComplete(int result) {
  if (ProcessReadResult(result)) {
    StartReading();
  }
}

}  // namespace net
