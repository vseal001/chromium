// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_VR_SERVICE_XR_RUNTIME_MANAGER_H_
#define CHROME_BROWSER_VR_SERVICE_XR_RUNTIME_MANAGER_H_

#include <stdint.h>

#include <map>
#include <memory>
#include <vector>

#include "base/callback.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/threading/thread_checker.h"
#include "base/timer/timer.h"
#include "chrome/browser/vr/service/vr_service_impl.h"
#include "chrome/browser/vr/vr_export.h"
#include "device/vr/public/mojom/vr_service.mojom.h"
#include "device/vr/vr_device.h"
#include "mojo/public/cpp/bindings/binding_set.h"

namespace device {
class VRDeviceProvider;
}

namespace vr {

class BrowserXRRuntime;

// Singleton used to provide the platform's VR devices to VRServiceImpl
// instances.
class VR_EXPORT XRRuntimeManager {
 public:
  virtual ~XRRuntimeManager();

  // Returns the XRRuntimeManager singleton.
  static XRRuntimeManager* GetInstance();
  static bool HasInstance();
  static void RecordVrStartupHistograms();

  // Adds a listener for runtime manager events. XRRuntimeManager does not own
  // this object.
  // Automatically connects all currently available VR devices by querying
  // the device providers and, for each returned device, calling
  // VRServiceImpl::ConnectDevice.
  void AddService(VRServiceImpl* service);
  void RemoveService(VRServiceImpl* service);

  BrowserXRRuntime* GetRuntimeForOptions(
      device::mojom::XRSessionOptions* options);
  BrowserXRRuntime* GetImmersiveRuntime();

 protected:
  using ProviderList = std::vector<std::unique_ptr<device::VRDeviceProvider>>;

  // Used by tests to supply providers.
  explicit XRRuntimeManager(ProviderList providers);
  // Used by tests to check on device state.
  device::mojom::XRRuntime* GetRuntimeForTest(unsigned int id);

  size_t NumberOfConnectedServices();

 private:
  void InitializeProviders();
  void OnProviderInitialized();
  bool AreAllProvidersInitialized();

  void AddRuntime(unsigned int id,
                  device::mojom::VRDisplayInfoPtr info,
                  device::mojom::XRRuntimePtr runtime);
  void RemoveRuntime(unsigned int id);

  BrowserXRRuntime* GetRuntime(device::VRDeviceId id);

  ProviderList providers_;

  // VRDevices are owned by their providers, each correspond to a
  // BrowserXRRuntime that is owned by XRRuntimeManager.
  using DeviceRuntimeMap = base::small_map<
      std::map<unsigned int, std::unique_ptr<BrowserXRRuntime>>>;
  DeviceRuntimeMap runtimes_;

  bool providers_initialized_ = false;
  size_t num_initialized_providers_ = 0;

  std::set<VRServiceImpl*> services_;

  THREAD_CHECKER(thread_checker_);

  DISALLOW_COPY_AND_ASSIGN(XRRuntimeManager);
};

}  // namespace vr

#endif  // CHROME_BROWSER_VR_SERVICE_XR_RUNTIME_MANAGER_H_
