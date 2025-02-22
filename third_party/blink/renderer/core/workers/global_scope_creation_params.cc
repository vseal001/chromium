// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/workers/global_scope_creation_params.h"

#include <memory>
#include "third_party/blink/renderer/platform/network/content_security_policy_parsers.h"

namespace blink {

GlobalScopeCreationParams::GlobalScopeCreationParams(
    const KURL& script_url,
    ScriptType script_type,
    const String& user_agent,
    const Vector<CSPHeaderAndType>& content_security_policy_parsed_headers,
    ReferrerPolicy referrer_policy,
    const SecurityOrigin* starter_origin,
    bool starter_secure_context,
    WorkerClients* worker_clients,
    mojom::IPAddressSpace address_space,
    const Vector<String>* origin_trial_tokens,
    const base::UnguessableToken& parent_devtools_token,
    std::unique_ptr<WorkerSettings> worker_settings,
    V8CacheOptions v8_cache_options,
    WorkletModuleResponsesMap* module_responses_map,
    service_manager::mojom::blink::InterfaceProviderPtrInfo
        interface_provider_info,
    BeginFrameProviderParams begin_frame_provider_params,
    const FeaturePolicy* parent_feature_policy,
    base::UnguessableToken agent_cluster_id)
    : script_url(script_url.Copy()),
      script_type(script_type),
      user_agent(user_agent.IsolatedCopy()),
      referrer_policy(referrer_policy),
      starter_origin(starter_origin ? starter_origin->IsolatedCopy() : nullptr),
      starter_secure_context(starter_secure_context),
      worker_clients(worker_clients),
      address_space(address_space),
      parent_devtools_token(parent_devtools_token),
      worker_settings(std::move(worker_settings)),
      v8_cache_options(v8_cache_options),
      module_responses_map(module_responses_map),
      interface_provider(std::move(interface_provider_info)),
      begin_frame_provider_params(std::move(begin_frame_provider_params)),
      // At the moment, workers do not support their container policy being set,
      // so it will just be an empty ParsedFeaturePolicy for now.
      worker_feature_policy(FeaturePolicy::CreateFromParentPolicy(
          parent_feature_policy,
          ParsedFeaturePolicy() /* container_policy */,
          starter_origin->ToUrlOrigin())),
      agent_cluster_id(agent_cluster_id) {
  this->content_security_policy_parsed_headers.ReserveInitialCapacity(
      content_security_policy_parsed_headers.size());
  for (const auto& header : content_security_policy_parsed_headers) {
    this->content_security_policy_parsed_headers.emplace_back(
        header.first.IsolatedCopy(), header.second);
  }

  this->origin_trial_tokens = std::make_unique<Vector<String>>();
  if (origin_trial_tokens) {
    for (const String& token : *origin_trial_tokens)
      this->origin_trial_tokens->push_back(token.IsolatedCopy());
  }
}

}  // namespace blink
