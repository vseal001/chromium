// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_SYSTEM_AUDIO_UNIFIED_VOLUME_VIEW_H_
#define ASH_SYSTEM_AUDIO_UNIFIED_VOLUME_VIEW_H_

#include "ash/system/unified/unified_slider_view.h"
#include "chromeos/audio/cras_audio_handler.h"

namespace ash {

class UnifiedVolumeSliderController;

// View of a slider that can change audio volume.
class UnifiedVolumeView : public UnifiedSliderView,
                          public chromeos::CrasAudioHandler::AudioObserver {
 public:
  UnifiedVolumeView(UnifiedVolumeSliderController* controller,
                    bool is_main_view);
  ~UnifiedVolumeView() override;

  views::Button* more_button() { return more_button_; }

 private:
  void Update(bool by_user);

  // CrasAudioHandler::AudioObserver:
  void OnOutputNodeVolumeChanged(uint64_t node_id, int volume) override;
  void OnOutputMuteChanged(bool mute_on, bool system_adjust) override;
  void OnAudioNodesChanged() override;
  void OnActiveOutputNodeChanged() override;
  void OnActiveInputNodeChanged() override;

  // UnifiedSliderView:
  void ChildVisibilityChanged(views::View* child) override;

  views::Button* const more_button_;
  const bool is_main_view_;

  DISALLOW_COPY_AND_ASSIGN(UnifiedVolumeView);
};

}  // namespace ash

#endif  // ASH_SYSTEM_AUDIO_UNIFIED_VOLUME_VIEW_H_
