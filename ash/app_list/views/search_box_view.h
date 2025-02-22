// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_APP_LIST_VIEWS_SEARCH_BOX_VIEW_H_
#define ASH_APP_LIST_VIEWS_SEARCH_BOX_VIEW_H_

#include <vector>

#include "ash/app_list/app_list_export.h"
#include "ash/app_list/app_list_view_delegate.h"
#include "ash/app_list/model/search/search_box_model_observer.h"
#include "ash/public/cpp/app_list/app_list_types.h"
#include "ui/chromeos/search_box/search_box_view_base.h"

namespace views {
class Textfield;
class View;
}  // namespace views

namespace app_list {

class AppListView;
class AppListViewDelegate;
class ContentsView;
class SearchModel;

// Subclass of search_box::SearchBoxViewBase. SearchBoxModel is its data model
// that controls what icon to display, what placeholder text to use for
// Textfield. The text and selection model part could be set to change the
// contents and selection model of the Textfield.
class APP_LIST_EXPORT SearchBoxView : public search_box::SearchBoxViewBase,
                                      public SearchBoxModelObserver {
 public:
  SearchBoxView(search_box::SearchBoxViewDelegate* delegate,
                AppListViewDelegate* view_delegate,
                AppListView* app_list_view = nullptr);
  ~SearchBoxView() override;

  // Overridden from search_box::SearchBoxViewBase:
  void ClearSearch() override;
  views::View* GetSelectedViewInContentsView() override;
  void HandleSearchBoxEvent(ui::LocatedEvent* located_event) override;
  void ModelChanged() override;
  void UpdateKeyboardVisibility() override;
  void UpdateModel(bool initiated_by_user) override;
  void UpdateSearchIcon() override;
  void UpdateSearchBoxBorder() override;
  void SetupCloseButton() override;
  void SetupBackButton() override;
  void RecordSearchBoxActivationHistogram(ui::EventType event_type) override;

  // Overridden from views::View:
  void OnKeyEvent(ui::KeyEvent* event) override;
  bool OnMouseWheel(const ui::MouseWheelEvent& event) override;

  // Overridden from views::ButtonListener:
  void ButtonPressed(views::Button* sender, const ui::Event& event) override;

  // Updates the search box's background corner radius and color based on the
  // state of AppListModel.
  void UpdateBackground(double progress,
                        ash::AppListState current_state,
                        ash::AppListState target_state);

  // Updates the search box's layout based on the state of AppListModel.
  void UpdateLayout(double progress,
                    ash::AppListState current_state,
                    ash::AppListState target_state);

  // Returns background border corner radius in the given state.
  int GetSearchBoxBorderCornerRadiusForState(ash::AppListState state) const;

  // Returns background color for the given state.
  SkColor GetBackgroundColorForState(ash::AppListState state) const;

  // Updates the opacity of the searchbox.
  void UpdateOpacity();

  // Shows Zero State suggestions.
  void ShowZeroStateSuggestions();

  // Called when the wallpaper colors change.
  void OnWallpaperColorsChanged();

  // Sets the autocomplete text if autocomplete conditions are met.
  void ProcessAutocomplete();

  void set_contents_view(ContentsView* contents_view) {
    contents_view_ = contents_view;
  }
  ContentsView* contents_view() { return contents_view_; }

 private:
  // Gets the wallpaper prominent colors.
  void GetWallpaperProminentColors(
      AppListViewDelegate::GetWallpaperProminentColorsCallback callback);

  // Callback invoked when the wallpaper prominent colors are returned after
  // calling |AppListViewDelegate::GetWallpaperProminentColors|.
  void OnWallpaperProminentColorsReceived(
      const std::vector<SkColor>& prominent_colors);

  // Notifies SearchBoxViewDelegate that the autocomplete text is valid.
  void AcceptAutocompleteText();

  // Accepts one character in the autocomplete text and fires query.
  void AcceptOneCharInAutocompleteText();

  // Removes all autocomplete text.
  void ClearAutocompleteText();

  // After verifying autocomplete text is valid, sets the current searchbox
  // text to the autocomplete text and sets the text highlight.
  void SetAutocompleteText(const base::string16& autocomplete_text);

  // Overridden from views::TextfieldController:
  void ContentsChanged(views::Textfield* sender,
                       const base::string16& new_contents) override;
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& key_event) override;
  bool HandleMouseEvent(views::Textfield* sender,
                        const ui::MouseEvent& mouse_event) override;

  // Overridden from SearchBoxModelObserver:
  void HintTextChanged() override;
  void SelectionModelChanged() override;
  void Update() override;
  void SearchEngineChanged() override;

  // The range of highlighted text for autocomplete.
  gfx::Range highlight_range_;

  // The key most recently pressed.
  ui::KeyboardCode last_key_pressed_ = ui::VKEY_UNKNOWN;

  AppListViewDelegate* view_delegate_;   // Not owned.
  SearchModel* search_model_ = nullptr;  // Owned by the profile-keyed service.

  // Owned by views hierarchy.
  app_list::AppListView* app_list_view_;
  ContentsView* contents_view_ = nullptr;

  // True if new style launcher feature is enabled.
  const bool is_new_style_launcher_enabled_;

  base::WeakPtrFactory<SearchBoxView> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(SearchBoxView);
};

}  // namespace app_list

#endif  // ASH_APP_LIST_VIEWS_SEARCH_BOX_VIEW_H_
