// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device/fido/fido_request_handler_base.h"

#include <utility>

#include "base/logging.h"
#include "base/strings/string_piece.h"
#include "build/build_config.h"
#include "device/fido/fido_device.h"
#include "device/fido/fido_task.h"
#include "services/service_manager/public/cpp/connector.h"

namespace device {

namespace {

bool ShouldDeferRequestDispatchToUi(const FidoAuthenticator& authenticator) {
  // TODO(hongjunchoi): Change this to be dependent on authenticator transport
  // type once UI component is in place.
  return false;
}

}  // namespace

FidoRequestHandlerBase::AuthenticatorMapObserver::~AuthenticatorMapObserver() =
    default;

FidoRequestHandlerBase::FidoRequestHandlerBase(
    service_manager::Connector* connector,
    const base::flat_set<FidoTransportProtocol>& transports)
    : FidoRequestHandlerBase(connector,
                             transports,
                             AddPlatformAuthenticatorCallback()) {}

FidoRequestHandlerBase::FidoRequestHandlerBase(
    service_manager::Connector* connector,
    const base::flat_set<FidoTransportProtocol>& transports,
    AddPlatformAuthenticatorCallback add_platform_authenticator)
    : add_platform_authenticator_(std::move(add_platform_authenticator)) {
  for (const auto transport : transports) {
    // Construction of CaBleDiscovery is handled by the implementing class as it
    // requires an extension passed on from the relying party.
    if (transport == FidoTransportProtocol::kCloudAssistedBluetoothLowEnergy)
      continue;

    if (transport == FidoTransportProtocol::kInternal) {
      // Internal authenticators are injected through
      // AddPlatformAuthenticatorCallback.
      NOTREACHED();
      continue;
    }

    auto discovery = FidoDiscovery::Create(transport, connector);
    if (discovery == nullptr) {
      // This can occur in tests when a ScopedVirtualU2fDevice is in effect and
      // HID transports are not configured.
      continue;
    }
    discovery->set_observer(this);
    discoveries_.push_back(std::move(discovery));
  }
}

FidoRequestHandlerBase::~FidoRequestHandlerBase() = default;

void FidoRequestHandlerBase::CancelOngoingTasks(
    base::StringPiece exclude_device_id) {
  for (auto task_it = active_authenticators_.begin();
       task_it != active_authenticators_.end();) {
    DCHECK(!task_it->first.empty());
    if (task_it->first != exclude_device_id) {
      DCHECK(task_it->second);
      task_it->second->Cancel();
      task_it = active_authenticators_.erase(task_it);
    } else {
      ++task_it;
    }
  }
}

void FidoRequestHandlerBase::Start() {
  for (const auto& discovery : discoveries_)
    discovery->Start();

  MaybeAddPlatformAuthenticator();
}

void FidoRequestHandlerBase::MaybeAddPlatformAuthenticator() {
  if (!add_platform_authenticator_)
    return;

  auto authenticator = std::move(add_platform_authenticator_).Run();
  if (!authenticator)
    return;

  AddAuthenticator(std::move(authenticator));
}

void FidoRequestHandlerBase::DiscoveryStarted(FidoDiscovery* discovery,
                                              bool success) {
  if (discovery->transport() == FidoTransportProtocol::kBluetoothLowEnergy &&
      observer_) {
    observer_->BluetoothAdapterIsAvailable();
  }
}

void FidoRequestHandlerBase::DeviceAdded(FidoDiscovery* discovery,
                                         FidoDevice* device) {
  DCHECK(!base::ContainsKey(active_authenticators(), device->GetId()));
  AddAuthenticator(CreateAuthenticatorFromDevice(device));
}

std::unique_ptr<FidoDeviceAuthenticator>
FidoRequestHandlerBase::CreateAuthenticatorFromDevice(FidoDevice* device) {
  return std::make_unique<FidoDeviceAuthenticator>(device);
}

void FidoRequestHandlerBase::DeviceRemoved(FidoDiscovery* discovery,
                                           FidoDevice* device) {
  // Device connection has been lost or device has already been removed.
  // Thus, calling CancelTask() is not necessary. Also, below
  // ongoing_tasks_.erase() will have no effect for the devices that have been
  // already removed due to processing error or due to invocation of
  // CancelOngoingTasks().
  DCHECK(device);
  active_authenticators_.erase(device->GetId());

  if (observer_)
    observer_->FidoAuthenticatorRemoved(device->GetId());
}

void FidoRequestHandlerBase::AddAuthenticator(
    std::unique_ptr<FidoAuthenticator> authenticator) {
  DCHECK(authenticator &&
         !base::ContainsKey(active_authenticators(), authenticator->GetId()));
  FidoAuthenticator* authenticator_ptr = authenticator.get();
  active_authenticators_.emplace(authenticator->GetId(),
                                 std::move(authenticator));

  if (!ShouldDeferRequestDispatchToUi(*authenticator_ptr))
    DispatchRequest(authenticator_ptr);

  if (observer_)
    observer_->FidoAuthenticatorAdded(*authenticator_ptr);
}

}  // namespace device
