/*
 * Copyright (C) 2009 Google Inc. All rights reserved.
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

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_EXPORTED_WEB_FILE_CHOOSER_COMPLETION_IMPL_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_EXPORTED_WEB_FILE_CHOOSER_COMPLETION_IMPL_H_

#include "base/memory/scoped_refptr.h"
#include "third_party/blink/public/platform/web_string.h"
#include "third_party/blink/public/platform/web_vector.h"
#include "third_party/blink/public/web/web_file_chooser_completion.h"
#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/core/html/forms/file_chooser.h"

namespace blink {

class ChromeClientImpl;
class LocalFrame;

class CORE_EXPORT WebFileChooserCompletionImpl final
    : public WebFileChooserCompletion {
 public:
  explicit WebFileChooserCompletionImpl(
      scoped_refptr<FileChooser> chooser,
      ChromeClientImpl* chrome_client = nullptr);
  ~WebFileChooserCompletionImpl() override;
  void DidChooseFile(const WebVector<WebString>& file_names) override;
  void DidChooseFile(const WebVector<SelectedFileInfo>& files) override;

  const WebFileChooserParams& Params() const { return file_chooser_->Params(); }
  LocalFrame* FrameOrNull() const { return file_chooser_->FrameOrNull(); }

 private:
  scoped_refptr<FileChooser> file_chooser_;

  // chrome_client_impl_ is nullptr in a case of
  // EnumerateChosenDirectory(). We don't need to call back ChromeClientImpl.
  Persistent<ChromeClientImpl> chrome_client_impl_;
};

}  // namespace blink

#endif
