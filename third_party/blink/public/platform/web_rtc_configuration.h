/*
 * Copyright (C) 2012 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef THIRD_PARTY_BLINK_PUBLIC_PLATFORM_WEB_RTC_CONFIGURATION_H_
#define THIRD_PARTY_BLINK_PUBLIC_PLATFORM_WEB_RTC_CONFIGURATION_H_

#include "third_party/blink/public/platform/web_common.h"
#include "third_party/blink/public/platform/web_rtc_certificate.h"
#include "third_party/blink/public/platform/web_vector.h"
#include "third_party/webrtc/api/peerconnectioninterface.h"

#include <memory>

namespace blink {

// This is distinct from webrtc::SdpSemantics to add the kDefault option.
enum class WebRTCSdpSemantics { kDefault, kPlanB, kUnifiedPlan };

struct WebRTCConfiguration {
  std::vector<webrtc::PeerConnectionInterface::IceServer> ice_servers;
  webrtc::PeerConnectionInterface::IceTransportsType ice_transport_policy =
      webrtc::PeerConnectionInterface::kAll;
  webrtc::PeerConnectionInterface::BundlePolicy bundle_policy =
      webrtc::PeerConnectionInterface::kBundlePolicyBalanced;
  webrtc::PeerConnectionInterface::RtcpMuxPolicy rtcp_mux_policy =
      webrtc::PeerConnectionInterface::kRtcpMuxPolicyRequire;
  WebVector<std::unique_ptr<WebRTCCertificate>> certificates;
  int ice_candidate_pool_size = 0;
  WebRTCSdpSemantics sdp_semantics = WebRTCSdpSemantics::kDefault;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_PUBLIC_PLATFORM_WEB_RTC_CONFIGURATION_H_
