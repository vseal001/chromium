// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.ntp.cards;

import android.support.annotation.Nullable;
import android.text.TextUtils;

import org.chromium.base.Callback;
import org.chromium.base.Log;
import org.chromium.chrome.browser.ChromeFeatureList;
import org.chromium.chrome.browser.modelutil.ListObservable;
import org.chromium.chrome.browser.modelutil.PropertyListObservable;
import org.chromium.chrome.browser.modelutil.SimpleListObservableBase;
import org.chromium.chrome.browser.modelutil.SimpleRecyclerViewMcpBase;
import org.chromium.chrome.browser.ntp.NewTabPageUma;
import org.chromium.chrome.browser.ntp.cards.NewTabPageViewHolder.PartialBindCallback;
import org.chromium.chrome.browser.ntp.snippets.CategoryInt;
import org.chromium.chrome.browser.ntp.snippets.CategoryStatus;
import org.chromium.chrome.browser.ntp.snippets.KnownCategories;
import org.chromium.chrome.browser.ntp.snippets.SectionHeader;
import org.chromium.chrome.browser.ntp.snippets.SnippetArticle;
import org.chromium.chrome.browser.ntp.snippets.SnippetArticleViewHolder;
import org.chromium.chrome.browser.ntp.snippets.SnippetsBridge;
import org.chromium.chrome.browser.ntp.snippets.SuggestionsSource;
import org.chromium.chrome.browser.offlinepages.OfflinePageBridge;
import org.chromium.chrome.browser.offlinepages.OfflinePageItem;
import org.chromium.chrome.browser.preferences.Pref;
import org.chromium.chrome.browser.preferences.PrefServiceBridge;
import org.chromium.chrome.browser.suggestions.SuggestionsOfflineModelObserver;
import org.chromium.chrome.browser.suggestions.SuggestionsRanker;
import org.chromium.chrome.browser.suggestions.SuggestionsUiDelegate;

import java.util.Arrays;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Set;

/**
 * A group of suggestions, with a header, a status card, and a progress indicator. This is
 * responsible for tracking whether its suggestions have been saved offline.
 */
public class SuggestionsSection extends InnerNode<NewTabPageViewHolder, PartialBindCallback> {
    private static final String TAG = "NtpCards";

    private final Delegate mDelegate;
    private final SuggestionsCategoryInfo mCategoryInfo;
    private final OfflineModelObserver mOfflineModelObserver;
    private final SuggestionsSource mSuggestionsSource;
    private final SuggestionsRanker mSuggestionsRanker;

    private final PropertyListObservable<SnippetArticle, PartialBindCallback> mSuggestions =
            new PropertyListObservable<>();

    // Children
    private final SectionHeader mHeader;
    private final SuggestionsList mSuggestionsList;
    private final StatusItem mStatus;
    private final ActionItem mMoreButton;

    /**
     * Stores whether any suggestions have been appended to the list. In this case the list can
     * generally be longer than what is served by the Source. Thus, the list should never be
     * replaced again.
     */
    private boolean mHasAppended;

    /**
     * Whether the data displayed by this section is not the latest available and should be updated
     * when the user stops interacting with this UI surface.
     */
    private boolean mIsDataStale;

    /**
     * Whether the section has been destroyed.
     */
    private boolean mIsDestroyed;

    /**
     * Delegate interface that allows dismissing this section without introducing
     * a circular dependency.
     */
    public interface Delegate {
        /**
         * Dismisses a section.
         * @param section The section to be dismissed.
         */
        void dismissSection(SuggestionsSection section);
    }

    public SuggestionsSection(Delegate delegate, SuggestionsUiDelegate uiDelegate,
            SuggestionsRanker ranker, OfflinePageBridge offlinePageBridge,
            SuggestionsCategoryInfo info) {
        mDelegate = delegate;
        mCategoryInfo = info;
        mSuggestionsSource = uiDelegate.getSuggestionsSource();
        mSuggestionsRanker = ranker;

        boolean isExpandable = ChromeFeatureList.isEnabled(
                                       ChromeFeatureList.NTP_ARTICLE_SUGGESTIONS_EXPANDABLE_HEADER)
                && getCategory() == KnownCategories.ARTICLES;
        boolean isExpanded =
                PrefServiceBridge.getInstance().getBoolean(Pref.NTP_ARTICLES_LIST_VISIBLE);
        mHeader = isExpandable ? new SectionHeader(info.getTitle(), isExpanded,
                                         this::updateSuggestionsVisibilityForExpandableHeader)
                               : new SectionHeader(info.getTitle());
        mSuggestionsList =
                new SuggestionsList(mSuggestionsSource, mSuggestions, this::bindSuggestion);
        mMoreButton = new ActionItem(this, ranker);

        mStatus = StatusItem.createNoSuggestionsItem(info);
        mStatus.setVisible(shouldShowStatusItem());
        addChildren(mHeader, mSuggestionsList, mStatus, mMoreButton);

        mOfflineModelObserver = new OfflineModelObserver(offlinePageBridge);
        uiDelegate.addDestructionObserver(mOfflineModelObserver);
    }

    private static class SuggestionsList extends SimpleRecyclerViewMcpBase<SnippetArticle,
            NewTabPageViewHolder, PartialBindCallback> {
        private final SuggestionsSource mSuggestionsSource;
        private final SimpleListObservableBase<SnippetArticle, PartialBindCallback> mSuggestions;

        private boolean mIsDestroyed;

        public SuggestionsList(SuggestionsSource suggestionsSource,
                PropertyListObservable<SnippetArticle, PartialBindCallback> suggestions,
                ViewBinder<SnippetArticle, NewTabPageViewHolder, PartialBindCallback> viewBinder) {
            super(ignored -> ItemViewType.SNIPPET, viewBinder, suggestions);
            mSuggestionsSource = suggestionsSource;
            mSuggestions = suggestions;
        }

        @Override
        public String describeItemForTesting(int position) {
            return String.format(
                    Locale.US, "SUGGESTION(%1.42s)", mSuggestions.get(position).mTitle);
        }

        @Override
        public Set<Integer> getItemDismissalGroup(int position) {
            return Collections.singleton(position);
        }

        @Override
        public void dismissItem(int position, Callback<String> itemRemovedCallback) {
            if (mIsDestroyed) {
                // It is possible for this method to be called after the NewTabPage has had
                // destroy() called. This can happen when
                // NewTabPageRecyclerView.dismissWithAnimation() is called and the animation ends
                // after the user has navigated away. In this case we cannot inform the native side
                // that the snippet has been dismissed (http://crbug.com/649299).
                return;
            }

            SnippetArticle suggestion = mSuggestions.removeAt(position);
            mSuggestionsSource.dismissSuggestion(suggestion);
            itemRemovedCallback.onResult(suggestion.mTitle);
        }

        public void updateSuggestionOfflineId(int position, Long newId, boolean isPrefetched) {
            SnippetArticle article = mSuggestions.get(position);
            // The suggestions could have been removed / replaced in the meantime.
            if (position == -1) return;

            Long oldId = article.getOfflinePageOfflineId();
            article.setOfflinePageOfflineId(newId);
            article.setIsPrefetched(isPrefetched);

            // TODO(bauerb): This notification should be sent by the article itself.
            if ((oldId == null) == (newId == null)) return;
            notifyItemChanged(position, SnippetArticleViewHolder::refreshOfflineBadgeVisibility);
        }

        public void destroy() {
            assert !mIsDestroyed;
            mIsDestroyed = true;
        }
    }

    public void destroy() {
        assert !mIsDestroyed;
        mOfflineModelObserver.onDestroy();
        mSuggestionsList.destroy();
        mIsDestroyed = true;
    }

    private void onSuggestionsListCountChanged(int oldSuggestionsCount) {
        int newSuggestionsCount = getSuggestionsCount();
        if ((newSuggestionsCount == 0) == (oldSuggestionsCount == 0)) return;

        mStatus.setVisible(shouldShowStatusItem());

        // When the ActionItem stops being dismissable, it is possible that it was being
        // interacted with. We need to reset the view's related property changes.
        mMoreButton.maybeResetForDismiss();
    }

    @Override
    public void dismissItem(int position, Callback<String> itemRemovedCallback) {
        if (getSectionDismissalRange().contains(position)) {
            mDelegate.dismissSection(this);
            itemRemovedCallback.onResult(getHeaderText());
            return;
        }
        super.dismissItem(position, itemRemovedCallback);
    }

    @Override
    public void onItemRangeRemoved(ListObservable child, int index, int count) {
        super.onItemRangeRemoved(child, index, count);
        if (child == mSuggestionsList) onSuggestionsListCountChanged(getSuggestionsCount() + count);
    }

    @Override
    public void onItemRangeInserted(ListObservable child, int index, int count) {
        super.onItemRangeInserted(child, index, count);
        if (child == mSuggestionsList) {
            onSuggestionsListCountChanged(getSuggestionsCount() - count);
        }
    }

    @Override
    protected void notifyItemRangeInserted(int index, int count) {
        super.notifyItemRangeInserted(index, count);
        notifyNeighboursModified(index - 1, index + count);
    }

    @Override
    protected void notifyItemRangeRemoved(int index, int count) {
        super.notifyItemRangeRemoved(index, count);
        notifyNeighboursModified(index - 1, index);
    }

    /** Sends a notification to the items at the provided indices to refresh their background. */
    private void notifyNeighboursModified(int aboveNeighbour, int belowNeighbour) {
        assert aboveNeighbour < belowNeighbour;

        if (aboveNeighbour >= 0) {
            notifyItemChanged(aboveNeighbour, NewTabPageViewHolder::updateLayoutParams);
        }

        if (belowNeighbour < getItemCount()) {
            notifyItemChanged(belowNeighbour, NewTabPageViewHolder::updateLayoutParams);
        }
    }

    /**
     * Removes a suggestion. Does nothing if the ID is unknown.
     * @param idWithinCategory The ID of the suggestion to remove.
     */
    public void removeSuggestionById(String idWithinCategory) {
        int i = 0;
        for (SnippetArticle suggestion : mSuggestions) {
            if (suggestion.mIdWithinCategory.equals(idWithinCategory)) {
                mSuggestions.removeAt(i);
                return;
            }
            i++;
        }
    }

    private int getNumberOfSuggestionsExposed() {
        int exposedCount = 0;
        int suggestionsCount = 0;
        for (SnippetArticle suggestion : mSuggestions) {
            ++suggestionsCount;
            // We treat all suggestions preceding an exposed suggestion as exposed too.
            if (suggestion.mExposed) exposedCount = suggestionsCount;
        }

        return exposedCount;
    }

    private boolean hasSuggestions() {
        return mSuggestions.size() != 0;
    }

    public int getSuggestionsCount() {
        return mSuggestions.size();
    }

    public SnippetArticle getSuggestionForTesting(int index) {
        return mSuggestions.get(index);
    }

    public boolean isDataStale() {
        return mIsDataStale;
    }

    /** Whether the section is waiting for content to be loaded. */
    public boolean isLoading() {
        return mMoreButton.getState() == ActionItem.State.LOADING;
    }

    /**
     * @return Whether the section is showing content cards. The placeholder is included in this
     * check, as it's standing for content, but the status card is not.
     */
    public boolean hasCards() {
        return hasSuggestions();
    }

    private String[] getDisplayedSuggestionIds() {
        String[] suggestionIds = new String[mSuggestions.size()];
        for (int i = 0; i < mSuggestions.size(); ++i) {
            suggestionIds[i] = mSuggestions.get(i).mIdWithinCategory;
        }
        return suggestionIds;
    }

    /**
     * Requests the section to update itself. If possible, it will retrieve suggestions from the
     * backend and use them to replace the current ones. This call may have no or only partial
     * effect if changing the list of suggestions is not allowed (e.g. because the user has already
     * seen the suggestions). In that case, the section will be flagged as stale.
     * (see {@link #isDataStale()})
     * Note, that this method also gets called if the user hits the "More" button on an empty list
     * (either because all suggestions got dismissed or because they were removed due to privacy
     * reasons; e.g. a user clearing their history).
     */
    public void updateSuggestions() {
        int numberOfSuggestionsExposed = getNumberOfSuggestionsExposed();
        if (!canUpdateSuggestions(numberOfSuggestionsExposed)) {
            mIsDataStale = true;
            Log.d(TAG, "updateSuggestions: Category %d is stale, it can't replace suggestions.",
                    getCategory());
            return;
        }

        List<SnippetArticle> suggestions =
                mSuggestionsSource.getSuggestionsForCategory(getCategory());
        Log.d(TAG, "Received %d new suggestions for category %d, had %d previously.",
                suggestions.size(), getCategory(), mSuggestions.size());

        // Nothing to append, we can just exit now.
        // TODO(dgn): Distinguish the init case where we have to wait? (https://crbug.com/711457)
        if (suggestions.isEmpty()) return;

        if (numberOfSuggestionsExposed > 0) {
            mIsDataStale = true;
            Log.d(TAG,
                    "updateSuggestions: Category %d is stale, will keep already seen suggestions.",
                    getCategory());
        }
        appendSuggestions(suggestions, /* keepSectionSize = */ true,
                /* reportPrefetchedSuggestionsCount = */ false);
    }

    /**
     * Adds the provided suggestions to the ones currently displayed by the section.
     *
     * @param suggestions The suggestions to be added at the end of the current list.
     * @param keepSectionSize Whether the section size should stay the same -- will be enforced by
     *         replacing not-yet-seen suggestions with the new suggestions.
     * @param reportPrefetchedSuggestionsCount Whether to report the number of prefetched article
     *         suggestions.
     */
    public void appendSuggestions(List<SnippetArticle> suggestions, boolean keepSectionSize,
            boolean reportPrefetchedSuggestionsCount) {
        if (!shouldShowSuggestions()) return;

        int numberOfSuggestionsExposed = getNumberOfSuggestionsExposed();
        if (keepSectionSize) {
            Log.d(TAG, "updateSuggestions: keeping the first %d suggestion",
                    numberOfSuggestionsExposed);
            int numSuggestionsToAppend =
                    Math.max(0, suggestions.size() - numberOfSuggestionsExposed);
            int itemCount = mSuggestions.size();
            if (itemCount > numberOfSuggestionsExposed) {
                mSuggestions.removeRange(
                        numberOfSuggestionsExposed, itemCount - numberOfSuggestionsExposed);
            }
            trimIncomingSuggestions(suggestions, /* targetSize = */ numSuggestionsToAppend);
        }
        if (!suggestions.isEmpty()) {
            mSuggestions.addAll(suggestions);
        }

        mOfflineModelObserver.updateAllSuggestionsOfflineAvailability(
                reportPrefetchedSuggestionsCount);

        if (!keepSectionSize) {
            NewTabPageUma.recordUIUpdateResult(
                    NewTabPageUma.ContentSuggestionsUIUpdateResult.SUCCESS_APPENDED);
            mHasAppended = true;
        } else {
            NewTabPageUma.recordNumberOfSuggestionsSeenBeforeUIUpdateSuccess(
                    numberOfSuggestionsExposed);
            NewTabPageUma.recordUIUpdateResult(
                    NewTabPageUma.ContentSuggestionsUIUpdateResult.SUCCESS_REPLACED);
        }
    }

    /**
     * De-duplicates the new suggestions with the ones kept in {@link #mSuggestionsList} and removes
     * the excess of incoming items to make sure that the merged list has at most as many items as
     * the incoming list.
     */
    private void trimIncomingSuggestions(List<SnippetArticle> suggestions, int targetSize) {
        for (SnippetArticle suggestion : mSuggestions) {
            suggestions.remove(suggestion);
        }

        if (suggestions.size() > targetSize) {
            Log.d(TAG, "trimIncomingSuggestions: removing %d excess elements from the end",
                    suggestions.size() - targetSize);
            suggestions.subList(targetSize, suggestions.size()).clear();
        }
    }

    /**
     * Returns whether the list of suggestions can be updated at the moment.
     */
    private boolean canUpdateSuggestions(int numberOfSuggestionsExposed) {
        if (!shouldShowSuggestions()) return false;
        if (!hasSuggestions()) return true; // If we don't have any, we always accept updates.

        if (CardsVariationParameters.ignoreUpdatesForExistingSuggestions()) {
            Log.d(TAG, "updateSuggestions: replacing existing suggestion disabled");
            NewTabPageUma.recordUIUpdateResult(
                    NewTabPageUma.ContentSuggestionsUIUpdateResult.FAIL_DISABLED);
            return false;
        }

        if (numberOfSuggestionsExposed >= getSuggestionsCount() || mHasAppended) {
            // In case that suggestions got removed, we assume they already were seen. This might
            // be over-simplifying things, but given the rare occurences it should be good enough.
            Log.d(TAG, "updateSuggestions: replacing existing suggestion not possible, all seen");
            NewTabPageUma.recordUIUpdateResult(
                    NewTabPageUma.ContentSuggestionsUIUpdateResult.FAIL_ALL_SEEN);
            return false;
        }

        return true;
    }

    /**
     * Fetches additional suggestions only for this section.
     * @param onFailure A {@link Runnable} that will be run if the fetch fails.
     * @param onNoNewSuggestions A {@link Runnable} that will be run if the fetch succeeds but
     *                           provides no new suggestions.
     */
    public void fetchSuggestions(@Nullable final Runnable onFailure,
            @Nullable Runnable onNoNewSuggestions) {
        assert !isLoading();

        if (getSuggestionsCount() == 0 && getCategoryInfo().isRemote()) {
            // Trigger a full refresh of the section to ensure we persist content locally.
            // If a fetch can be made, the status will be synchronously updated from the backend.
            mSuggestionsSource.fetchRemoteSuggestions();
            return;
        }

        mMoreButton.updateState(ActionItem.State.LOADING);
        mSuggestionsSource.fetchSuggestions(mCategoryInfo.getCategory(),
                getDisplayedSuggestionIds(), suggestions -> { /* successCallback */
                    if (mIsDestroyed) return; // The section has been dismissed.

                    mMoreButton.updateState(ActionItem.State.BUTTON);

                    appendSuggestions(suggestions, /* keepSectionSize = */ false,
                            /* reportPrefetchedSuggestionsCount = */ false);
                    if (onNoNewSuggestions != null && suggestions.size() == 0) {
                        onNoNewSuggestions.run();
                    }
                }, () -> { /* failureRunnable */
                    if (mIsDestroyed) return; // The section has been dismissed.

                    mMoreButton.updateState(ActionItem.State.BUTTON);
                    if (onFailure != null) onFailure.run();
                });
    }

    /** Sets the status for the section. Some statuses can cause the suggestions to be cleared. */
    public void setStatus(@CategoryStatus int status) {
        if (!SnippetsBridge.isCategoryStatusAvailable(status)) {
            clearData();
            Log.d(TAG, "setStatus: unavailable status, cleared suggestions.");
        }

        boolean isLoading = SnippetsBridge.isCategoryLoading(status);
        mMoreButton.updateState(!shouldShowSuggestions()
                        ? ActionItem.State.HIDDEN
                        : (isLoading ? ActionItem.State.LOADING : ActionItem.State.BUTTON));
    }

    /** Clears the suggestions and related data, resetting the state of the section. */
    public void clearData() {
        mSuggestions.set(Collections.emptyList());
        mHasAppended = false;
        mIsDataStale = false;
    }

    @CategoryInt
    public int getCategory() {
        return mCategoryInfo.getCategory();
    }

    @Override
    public Set<Integer> getItemDismissalGroup(int position) {
        // The section itself can be dismissed via any of the items in the dismissal group,
        // otherwise we fall back to the default implementation, which dispatches to our children.
        Set<Integer> sectionDismissalRange = getSectionDismissalRange();
        if (sectionDismissalRange.contains(position)) return sectionDismissalRange;

        return super.getItemDismissalGroup(position);
    }

    /** Sets the visibility of this section's header. */
    public void setHeaderVisibility(boolean headerVisibility) {
        mHeader.setVisible(headerVisibility);
    }

    /**
     * @return Whether or not the suggestions should be shown in this section.
     */
    private boolean shouldShowSuggestions() {
        return !mHeader.isExpandable() || mHeader.isExpanded();
    }

    /**
     * @return Whether or not the {@link StatusItem} should be shown in this section.
     */
    private boolean shouldShowStatusItem() {
        return shouldShowSuggestions() && !hasSuggestions();
    }

    /**
     * @return The set of indices corresponding to items that can dismiss this entire section
     * (as opposed to individual items in it).
     */
    private Set<Integer> getSectionDismissalRange() {
        if (hasSuggestions()) return Collections.emptySet();

        int statusCardIndex = getStartingOffsetForChild(mStatus);
        if (!mMoreButton.isVisible()) return Collections.singleton(statusCardIndex);

        assert statusCardIndex + 1 == getStartingOffsetForChild(mMoreButton);
        return new HashSet<>(Arrays.asList(statusCardIndex, statusCardIndex + 1));
    }

    /**
     * Update the expandable header state to match the preference value if necessary. This can
     * happen when the preference is updated by a user click on another new tab page.
     */
    void updateExpandableHeader() {
        if (mHeader.isExpandable()
                && mHeader.isExpanded()
                        != PrefServiceBridge.getInstance().getBoolean(
                                   Pref.NTP_ARTICLES_LIST_VISIBLE)) {
            mHeader.toggleHeader();
        }
    }

    /**
     * Update the visibility of the suggestions based on whether the header is expanded. This is
     * called when the section header is toggled.
     */
    private void updateSuggestionsVisibilityForExpandableHeader() {
        assert mHeader.isExpandable();
        PrefServiceBridge.getInstance().setBoolean(
                Pref.NTP_ARTICLES_LIST_VISIBLE, mHeader.isExpanded());
        clearData();
        if (mHeader.isExpanded()) updateSuggestions();
        setStatus(mSuggestionsSource.getCategoryStatus(getCategory()));
        mStatus.setVisible(shouldShowStatusItem());
    }

    public SuggestionsCategoryInfo getCategoryInfo() {
        return mCategoryInfo;
    }

    public String getHeaderText() {
        return mHeader.getHeaderText();
    }

    ActionItem getActionItemForTesting() {
        return mMoreButton;
    }

    public SectionHeader getHeaderItemForTesting() {
        return mHeader;
    }

    private class OfflineModelObserver extends SuggestionsOfflineModelObserver<SnippetArticle> {
        public OfflineModelObserver(OfflinePageBridge bridge) {
            super(bridge);
        }

        @Override
        public void onSuggestionOfflineIdChanged(SnippetArticle suggestion, OfflinePageItem item) {
            boolean isPrefetched = item != null
                    && TextUtils.equals(item.getClientId().getNamespace(),
                               OfflinePageBridge.SUGGESTED_ARTICLES_NAMESPACE);

            mSuggestionsList.updateSuggestionOfflineId(mSuggestions.indexOf(suggestion),
                    item == null ? null : item.getOfflineId(), isPrefetched);
        }

        @Override
        public Iterable<SnippetArticle> getOfflinableSuggestions() {
            return mSuggestions;
        }
    }

    private void bindSuggestion(NewTabPageViewHolder holder, SnippetArticle suggestion,
            @Nullable PartialBindCallback callback) {
        if (callback != null) {
            callback.onResult(holder);
            return;
        }

        mSuggestionsRanker.rankSuggestion(suggestion);
        ((SnippetArticleViewHolder) holder).onBindViewHolder(suggestion, mCategoryInfo);
    }
}
