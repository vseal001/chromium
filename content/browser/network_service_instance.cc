// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/public/browser/network_service_instance.h"

#include "base/feature_list.h"
#include "content/browser/network_service_client.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/common/service_manager_connection.h"
#include "content/public/common/service_names.mojom.h"
#include "net/log/net_log_util.h"
#include "services/network/network_service.h"
#include "services/network/public/cpp/features.h"
#include "services/network/public/cpp/network_switches.h"
#include "services/service_manager/public/cpp/connector.h"

namespace content {

namespace {

network::mojom::NetworkServicePtr* g_network_service_ptr = nullptr;
network::NetworkService* g_network_service;

void CreateNetworkServiceOnIO(network::mojom::NetworkServiceRequest request) {
  if (g_network_service) {
    // GetNetworkServiceImpl() was already called and created the object, so
    // just bind it.
    g_network_service->Bind(std::move(request));
    return;
  }

  g_network_service = new network::NetworkService(
      nullptr, std::move(request), GetContentClient()->browser()->GetNetLog());
}

}  // namespace

network::mojom::NetworkService* GetNetworkService() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  if (!g_network_service_ptr)
    g_network_service_ptr = new network::mojom::NetworkServicePtr;
  static NetworkServiceClient* g_client;
  if (!g_network_service_ptr->is_bound() ||
      g_network_service_ptr->encountered_error()) {
    if (base::FeatureList::IsEnabled(network::features::kNetworkService)) {
      ServiceManagerConnection::GetForProcess()->GetConnector()->BindInterface(
          mojom::kNetworkServiceName, g_network_service_ptr);
    } else {
      DCHECK(!g_network_service_ptr->is_bound());
      BrowserThread::PostTask(
          BrowserThread::IO, FROM_HERE,
          base::BindOnce(CreateNetworkServiceOnIO,
                         mojo::MakeRequest(g_network_service_ptr)));
    }

    network::mojom::NetworkServiceClientPtr client_ptr;
    delete g_client;  // In case we're recreating the network service.
    g_client = new NetworkServiceClient(mojo::MakeRequest(&client_ptr));
    (*g_network_service_ptr)->SetClient(std::move(client_ptr));

    if (base::FeatureList::IsEnabled(network::features::kNetworkService)) {
      const base::CommandLine* command_line =
          base::CommandLine::ForCurrentProcess();
      if (command_line->HasSwitch(network::switches::kLogNetLog)) {
        base::FilePath log_path =
            command_line->GetSwitchValuePath(network::switches::kLogNetLog);

        base::DictionaryValue client_constants =
            GetContentClient()->GetNetLogConstants();

        base::File file(
            log_path, base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_WRITE);
        LOG_IF(ERROR, !file.IsValid())
            << "Failed opening: " << log_path.value();
        (*g_network_service_ptr)
            ->StartNetLog(std::move(file), std::move(client_constants));
      }
    }

    GetContentClient()->browser()->OnNetworkServiceCreated(
        g_network_service_ptr->get());
  }
  return g_network_service_ptr->get();
}

network::NetworkService* GetNetworkServiceImpl() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  DCHECK(!base::FeatureList::IsEnabled(network::features::kNetworkService));
  if (!g_network_service) {
    g_network_service = new network::NetworkService(
        nullptr, nullptr, GetContentClient()->browser()->GetNetLog());
  }

  return g_network_service;
}

void FlushNetworkServiceInstanceForTesting() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  if (g_network_service_ptr)
    g_network_service_ptr->FlushForTesting();
}

}  // namespace content
