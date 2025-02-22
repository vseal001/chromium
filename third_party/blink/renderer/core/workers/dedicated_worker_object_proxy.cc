/*
 * Copyright (C) 2013 Google Inc. All rights reserved.
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

#include "third_party/blink/renderer/core/workers/dedicated_worker_object_proxy.h"

#include <memory>
#include <utility>

#include "base/memory/ptr_util.h"
#include "third_party/blink/public/platform/platform.h"
#include "third_party/blink/public/platform/task_type.h"
#include "third_party/blink/renderer/bindings/core/v8/serialization/serialized_script_value.h"
#include "third_party/blink/renderer/bindings/core/v8/source_location.h"
#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/events/message_event.h"
#include "third_party/blink/renderer/core/execution_context/execution_context.h"
#include "third_party/blink/renderer/core/inspector/console_message.h"
#include "third_party/blink/renderer/core/inspector/thread_debugger.h"
#include "third_party/blink/renderer/core/workers/dedicated_worker_messaging_proxy.h"
#include "third_party/blink/renderer/core/workers/parent_execution_context_task_runners.h"
#include "third_party/blink/renderer/core/workers/worker_global_scope.h"
#include "third_party/blink/renderer/core/workers/worker_thread.h"
#include "third_party/blink/renderer/platform/cross_thread_functional.h"
#include "third_party/blink/renderer/platform/web_task_runner.h"
#include "third_party/blink/renderer/platform/wtf/functional.h"

namespace blink {

std::unique_ptr<DedicatedWorkerObjectProxy> DedicatedWorkerObjectProxy::Create(
    DedicatedWorkerMessagingProxy* messaging_proxy_weak_ptr,
    ParentExecutionContextTaskRunners* parent_execution_context_task_runners) {
  DCHECK(messaging_proxy_weak_ptr);
  return base::WrapUnique(new DedicatedWorkerObjectProxy(
      messaging_proxy_weak_ptr, parent_execution_context_task_runners));
}

DedicatedWorkerObjectProxy::~DedicatedWorkerObjectProxy() = default;

void DedicatedWorkerObjectProxy::PostMessageToWorkerObject(
    scoped_refptr<SerializedScriptValue> message,
    Vector<MessagePortChannel> channels,
    const v8_inspector::V8StackTraceId& stack_id) {
  PostCrossThreadTask(
      *GetParentExecutionContextTaskRunners()->Get(TaskType::kPostedMessage),
      FROM_HERE,
      CrossThreadBind(&DedicatedWorkerMessagingProxy::PostMessageToWorkerObject,
                      messaging_proxy_weak_ptr_, std::move(message),
                      WTF::Passed(std::move(channels)), stack_id));
}

void DedicatedWorkerObjectProxy::ProcessMessageFromWorkerObject(
    scoped_refptr<SerializedScriptValue> message,
    Vector<MessagePortChannel> channels,
    WorkerThread* worker_thread,
    const v8_inspector::V8StackTraceId& stack_id) {
  WorkerGlobalScope* global_scope =
      ToWorkerGlobalScope(worker_thread->GlobalScope());
  MessagePortArray* ports =
      MessagePort::EntanglePorts(*global_scope, std::move(channels));

  ThreadDebugger* debugger = ThreadDebugger::From(worker_thread->GetIsolate());
  debugger->ExternalAsyncTaskStarted(stack_id);
  global_scope->DispatchEvent(*MessageEvent::Create(ports, std::move(message)));
  debugger->ExternalAsyncTaskFinished(stack_id);
}

void DedicatedWorkerObjectProxy::ProcessUnhandledException(
    int exception_id,
    WorkerThread* worker_thread) {
  WorkerGlobalScope* global_scope =
      ToWorkerGlobalScope(worker_thread->GlobalScope());
  global_scope->ExceptionUnhandled(exception_id);
}

void DedicatedWorkerObjectProxy::ReportException(
    const String& error_message,
    std::unique_ptr<SourceLocation> location,
    int exception_id) {
  PostCrossThreadTask(
      *GetParentExecutionContextTaskRunners()->Get(TaskType::kInternalDefault),
      FROM_HERE,
      CrossThreadBind(&DedicatedWorkerMessagingProxy::DispatchErrorEvent,
                      messaging_proxy_weak_ptr_, error_message,
                      WTF::Passed(location->Clone()), exception_id));
}

void DedicatedWorkerObjectProxy::DidCreateWorkerGlobalScope(
    WorkerOrWorkletGlobalScope* global_scope) {
  DCHECK(!worker_global_scope_);
  worker_global_scope_ = ToWorkerGlobalScope(global_scope);
}

void DedicatedWorkerObjectProxy::DidEvaluateClassicScript(bool success) {
  PostCrossThreadTask(
      *GetParentExecutionContextTaskRunners()->Get(TaskType::kInternalDefault),
      FROM_HERE,
      CrossThreadBind(&DedicatedWorkerMessagingProxy::DidEvaluateScript,
                      messaging_proxy_weak_ptr_, success));
}

void DedicatedWorkerObjectProxy::DidEvaluateModuleScript(bool success) {
  PostCrossThreadTask(
      *GetParentExecutionContextTaskRunners()->Get(TaskType::kInternalDefault),
      FROM_HERE,
      CrossThreadBind(&DedicatedWorkerMessagingProxy::DidEvaluateScript,
                      messaging_proxy_weak_ptr_, success));
}

void DedicatedWorkerObjectProxy::WillDestroyWorkerGlobalScope() {
  worker_global_scope_ = nullptr;
}

DedicatedWorkerObjectProxy::DedicatedWorkerObjectProxy(
    DedicatedWorkerMessagingProxy* messaging_proxy_weak_ptr,
    ParentExecutionContextTaskRunners* parent_execution_context_task_runners)
    : ThreadedObjectProxyBase(parent_execution_context_task_runners),
      messaging_proxy_weak_ptr_(messaging_proxy_weak_ptr) {}

CrossThreadWeakPersistent<ThreadedMessagingProxyBase>
DedicatedWorkerObjectProxy::MessagingProxyWeakPtr() {
  return messaging_proxy_weak_ptr_;
}

}  // namespace blink
