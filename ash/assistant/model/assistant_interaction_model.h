// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_ASSISTANT_MODEL_ASSISTANT_INTERACTION_MODEL_H_
#define ASH_ASSISTANT_MODEL_ASSISTANT_INTERACTION_MODEL_H_

#include <memory>
#include <string>
#include <vector>

#include "base/macros.h"
#include "base/observer_list.h"

namespace ash {

class AssistantInteractionModelObserver;
class AssistantQuery;
class AssistantResponse;

// Enumeration of interaction input modalities.
enum class InputModality {
  kKeyboard,
  kStylus,
  kVoice,
};

// TODO(dmblack): This is an oversimplification. We will eventually want to
// distinctly represent listening/thinking/etc. states explicitly so they can
// be adequately represented in the UI.
// Enumeration of interaction states.
enum class InteractionState {
  kActive,
  kInactive,
};

// Enumeration of interaction mic states.
enum class MicState {
  kClosed,
  kOpen,
};

// Models the Assistant interaction. This includes query state, state of speech
// recognition, as well as a renderable AssistantResponse.
class AssistantInteractionModel {
 public:
  AssistantInteractionModel();
  ~AssistantInteractionModel();

  // Adds/removes the specified interaction model |observer|.
  void AddObserver(AssistantInteractionModelObserver* observer);
  void RemoveObserver(AssistantInteractionModelObserver* observer);

  // Resets the interaction to its initial state.
  void ClearInteraction();

  // Resets the interaction to its initial state. There are instances in which
  // we wish to clear the interaction but retain the committed query. Similarly,
  // there are instances in which we wish to retain the pending response that is
  // currently cached. For such instances, use |retain_committed_query| and
  // |retain_pending_response| respectively.
  void ClearInteraction(bool retain_committed_query,
                        bool retain_pending_response);

  // Sets the interaction state.
  void SetInteractionState(InteractionState interaction_state);

  // Returns the interaction state.
  InteractionState interaction_state() const { return interaction_state_; }

  // Updates the input modality for the interaction.
  void SetInputModality(InputModality input_modality);

  // Returns the input modality for the interaction.
  InputModality input_modality() const { return input_modality_; }

  // Updates the mic state for the interaction.
  void SetMicState(MicState mic_state);

  // Returns the mic state for the interaction.
  MicState mic_state() const { return mic_state_; }

  // Returns the committed query for the interaction.
  const AssistantQuery& committed_query() const { return *committed_query_; }

  // Clears the committed query for the interaction.
  void ClearCommittedQuery();

  // Updates the pending query for the interaction.
  void SetPendingQuery(std::unique_ptr<AssistantQuery> pending_query);

  // Returns the pending query for the interaction.
  const AssistantQuery& pending_query() const { return *pending_query_; }

  // Commits the pending query for the interaction.
  void CommitPendingQuery();

  // Clears the pending query for the interaction.
  void ClearPendingQuery();

  // Sets the pending response for the interaction.
  void SetPendingResponse(std::unique_ptr<AssistantResponse> response);

  // Returns the pending response for the interaction.
  AssistantResponse* pending_response() { return pending_response_.get(); }

  // Finalizes the pending response for the interaction.
  void FinalizePendingResponse();

  // Clears the pending response for the interaction.
  void ClearPendingResponse();

  // Returns the finalized response for the interaction.
  const AssistantResponse* response() const { return response_.get(); }

  // Clears the finalized response for the interaction.
  void ClearResponse();

  // Updates the speech level in dB.
  void SetSpeechLevel(float speech_level_db);

 private:
  void NotifyInteractionStateChanged();
  void NotifyInputModalityChanged();
  void NotifyMicStateChanged();
  void NotifyCommittedQueryChanged();
  void NotifyCommittedQueryCleared();
  void NotifyPendingQueryChanged();
  void NotifyPendingQueryCleared();
  void NotifyResponseChanged();
  void NotifyResponseCleared();
  void NotifySpeechLevelChanged(float speech_level_db);

  InteractionState interaction_state_ = InteractionState::kInactive;
  InputModality input_modality_ = InputModality::kKeyboard;
  MicState mic_state_ = MicState::kClosed;
  std::unique_ptr<AssistantQuery> committed_query_;
  std::unique_ptr<AssistantQuery> pending_query_;
  std::unique_ptr<AssistantResponse> pending_response_;
  std::unique_ptr<AssistantResponse> response_;

  base::ObserverList<AssistantInteractionModelObserver> observers_;

  DISALLOW_COPY_AND_ASSIGN(AssistantInteractionModel);
};

}  // namespace ash

#endif  // ASH_ASSISTANT_MODEL_ASSISTANT_INTERACTION_MODEL_H_
