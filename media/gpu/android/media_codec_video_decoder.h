// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_GPU_ANDROID_MEDIA_CODEC_VIDEO_DECODER_H_
#define MEDIA_GPU_ANDROID_MEDIA_CODEC_VIDEO_DECODER_H_

#include "base/containers/circular_deque.h"
#include "base/optional.h"
#include "base/threading/thread_checker.h"
#include "base/timer/elapsed_timer.h"
#include "gpu/config/gpu_preferences.h"
#include "media/base/android_overlay_mojo_factory.h"
#include "media/base/overlay_info.h"
#include "media/base/video_decoder.h"
#include "media/gpu/android/android_video_surface_chooser.h"
#include "media/gpu/android/avda_codec_allocator.h"
#include "media/gpu/android/codec_wrapper.h"
#include "media/gpu/android/device_info.h"
#include "media/gpu/android/surface_chooser_helper.h"
#include "media/gpu/android/video_frame_factory.h"
#include "media/gpu/media_gpu_export.h"

namespace media {

struct PendingDecode {
  static PendingDecode CreateEos();
  PendingDecode(scoped_refptr<DecoderBuffer> buffer,
                VideoDecoder::DecodeCB decode_cb);
  PendingDecode(PendingDecode&& other);
  ~PendingDecode();

  scoped_refptr<DecoderBuffer> buffer;
  VideoDecoder::DecodeCB decode_cb;

 private:
  DISALLOW_COPY_AND_ASSIGN(PendingDecode);
};

// An Android VideoDecoder that delegates to MediaCodec.
//
// This decoder initializes in two stages. Low overhead initialization is done
// eagerly in Initialize(), but the rest is done lazily and is kicked off by the
// first Decode() (see StartLazyInit()). We do this because there are cases in
// our media pipeline where we'll initialize a decoder but never use it
// (e.g., MSE with no media data appended), and if we eagerly allocator decoder
// resources, like MediaCodecs and TextureOwners, we will block other
// playbacks that need them.
// TODO: Lazy initialization should be handled at a higher layer of the media
// stack for both simplicity and cross platform support.
class MEDIA_GPU_EXPORT MediaCodecVideoDecoder
    : public VideoDecoder,
      public AVDACodecAllocatorClient {
 public:
  MediaCodecVideoDecoder(
      const gpu::GpuPreferences& gpu_preferences,
      DeviceInfo* device_info,
      AVDACodecAllocator* codec_allocator,
      std::unique_ptr<AndroidVideoSurfaceChooser> surface_chooser,
      AndroidOverlayMojoFactoryCB overlay_factory_cb,
      RequestOverlayInfoCB request_overlay_info_cb,
      std::unique_ptr<VideoFrameFactory> video_frame_factory);

  // VideoDecoder implementation:
  std::string GetDisplayName() const override;
  void Initialize(
      const VideoDecoderConfig& config,
      bool low_delay,
      CdmContext* cdm_context,
      const InitCB& init_cb,
      const OutputCB& output_cb,
      const WaitingForDecryptionKeyCB& waiting_for_decryption_key_cb) override;
  void Decode(scoped_refptr<DecoderBuffer> buffer,
              const DecodeCB& decode_cb) override;
  void Reset(const base::Closure& closure) override;
  bool NeedsBitstreamConversion() const override;
  bool CanReadWithoutStalling() const override;
  int GetMaxDecodeRequests() const override;

 protected:
  // Protected for testing.
  ~MediaCodecVideoDecoder() override;

  // Set up |cdm_context| as part of initialization.  Guarantees that |init_cb|
  // will be called depending on the outcome, though not necessarily before this
  // function returns.
  void SetCdm(CdmContext* cdm_context, const InitCB& init_cb);

  // Called when the Cdm provides |media_crypto|.  Will signal |init_cb| based
  // on the result, and set the codec config properly.
  void OnMediaCryptoReady(const InitCB& init_cb,
                          JavaObjectPtr media_crypto,
                          bool requires_secure_video_codec);

 private:
  // The test has access for PumpCodec().
  friend class MediaCodecVideoDecoderTest;
  friend class base::DeleteHelper<MediaCodecVideoDecoder>;

  enum class State {
    // Initializing resources required to create a codec.
    kInitializing,
    // Initialization has completed and we're running. This is the only state
    // in which |codec_| might be non-null. If |codec_| is null, a codec
    // creation is pending.
    kRunning,
    // A fatal error occurred. A terminal state.
    kError,
    // The output surface was destroyed, but SetOutputSurface() is not supported
    // by the device. In this case the consumer is responsible for destroying us
    // soon, so this is terminal state but not a decode error.
    kSurfaceDestroyed
  };

  enum class DrainType { kForReset, kForDestroy };

  // Starts teardown.
  void Destroy() override;

  // Finishes initialization.
  void StartLazyInit();
  void OnVideoFrameFactoryInitialized(
      scoped_refptr<TextureOwner> texture_owner);

  // Resets |waiting_for_key_| to false, indicating that MediaCodec might now
  // accept buffers.
  void OnKeyAdded();

  // Updates |surface_chooser_| with the new overlay info.
  void OnOverlayInfoChanged(const OverlayInfo& overlay_info);
  void OnSurfaceChosen(std::unique_ptr<AndroidOverlay> overlay);
  void OnSurfaceDestroyed(AndroidOverlay* overlay);

  // Whether we have a codec and its surface is not equal to
  // |target_surface_bundle_|.
  bool SurfaceTransitionPending();

  // Sets |codecs_|'s output surface to |target_surface_bundle_|.
  void TransitionToTargetSurface();

  // Creates a codec asynchronously.
  void CreateCodec();

  // AVDACodecAllocatorClient implementation.
  void OnCodecConfigured(
      std::unique_ptr<MediaCodecBridge> media_codec,
      scoped_refptr<AVDASurfaceBundle> surface_bundle) override;

  // Flushes the codec, or if flush() is not supported, releases it and creates
  // a new one.
  void FlushCodec();

  // Attempts to queue input and dequeue output from the codec. Calls
  // StartTimerOrPumpCodec() even if the codec is idle when |force_start_timer|.
  void PumpCodec(bool force_start_timer);
  bool QueueInput();
  bool DequeueOutput();

  // Starts |pump_codec_timer_| if it's not started and resets the idle timeout.
  void StartTimerOrPumpCodec();
  void StopTimerIfIdle();

  // Runs |eos_decode_cb_| if it's valid and |reset_generation| matches
  // |reset_generation_|.
  void RunEosDecodeCb(int reset_generation);

  // Forwards |frame| via |output_cb_| if |reset_generation| matches
  // |reset_generation_|.
  void ForwardVideoFrame(int reset_generation,
                         const scoped_refptr<VideoFrame>& frame);

  // Starts draining the codec by queuing an EOS if required. It skips the drain
  // if possible.
  void StartDrainingCodec(DrainType drain_type);
  void OnCodecDrained();
  void CancelPendingDecodes(DecodeStatus status);

  // Sets |state_| and does common teardown for the terminal states. |state_|
  // must be either kSurfaceDestroyed or kError.
  void EnterTerminalState(State state);
  bool InTerminalState();

  // Releases |codec_| if it's not null.
  void ReleaseCodec();

  // Return true if we have a codec that's outputting to an overlay.
  bool IsUsingOverlay() const;

  // Notify us about a promotion hint.
  void NotifyPromotionHint(PromotionHintAggregator::Hint hint);

  // Update |cached_frame_information_|.
  void CacheFrameInformation();

  // Creates an overlay factory cb based on the value of overlay_info_.
  AndroidOverlayFactoryCB CreateOverlayFactoryCb();

  // Create a callback that will handle promotion hints, and set the overlay
  // position if required.
  PromotionHintAggregator::NotifyPromotionHintCB CreatePromotionHintCB();

  State state_ = State::kInitializing;

  // Whether initialization still needs to be done on the first decode call.
  bool lazy_init_pending_ = true;
  base::circular_deque<PendingDecode> pending_decodes_;

  // Whether we've seen MediaCodec return MEDIA_CODEC_NO_KEY indicating that
  // the corresponding key was not set yet, and MediaCodec will not accept
  // buffers until OnKeyAdded() is called.
  bool waiting_for_key_ = false;

  // The reason for the current drain operation if any.
  base::Optional<DrainType> drain_type_;

  // The current reset cb if a Reset() is in progress.
  base::Closure reset_cb_;

  // A generation counter that's incremented every time Reset() is called.
  int reset_generation_ = 0;

  // The EOS decode cb for an EOS currently being processed by the codec. Called
  // when the EOS is output.
  VideoDecoder::DecodeCB eos_decode_cb_;

  VideoDecoder::OutputCB output_cb_;
  VideoDecoderConfig decoder_config_;

  // Codec specific data (SPS and PPS for H264). Some MediaCodecs initialize
  // more reliably if we explicitly pass these (http://crbug.com/649185).
  std::vector<uint8_t> csd0_;
  std::vector<uint8_t> csd1_;

  std::unique_ptr<CodecWrapper> codec_;
  base::ElapsedTimer idle_timer_;
  base::RepeatingTimer pump_codec_timer_;
  AVDACodecAllocator* codec_allocator_;

  // The current target surface that |codec_| should be rendering to. It
  // reflects the latest surface choice by |surface_chooser_|. If the codec is
  // configured with some other surface, then a transition is pending. It's
  // non-null from the first surface choice.
  scoped_refptr<AVDASurfaceBundle> target_surface_bundle_;

  // A TextureOwner bundle that is kept for the lifetime of MCVD so that if we
  // have to synchronously switch surfaces we always have one available.
  scoped_refptr<AVDASurfaceBundle> texture_owner_bundle_;

  // A callback for requesting overlay info updates.
  RequestOverlayInfoCB request_overlay_info_cb_;

  // The current overlay info, which possibly specifies an overlay to render to.
  OverlayInfo overlay_info_;

  // The helper which manages our surface chooser for us.
  SurfaceChooserHelper surface_chooser_helper_;

  // The factory for creating VideoFrames from CodecOutputBuffers.
  std::unique_ptr<VideoFrameFactory> video_frame_factory_;

  // An optional factory callback for creating mojo AndroidOverlays.
  AndroidOverlayMojoFactoryCB overlay_factory_cb_;

  DeviceInfo* device_info_;
  bool enable_threaded_texture_mailboxes_;

  // Most recently cached frame information, so that we can dispatch it without
  // recomputing it on every frame.  It changes very rarely.
  SurfaceChooserHelper::FrameInformation cached_frame_information_ =
      SurfaceChooserHelper::FrameInformation::NON_OVERLAY_INSECURE;

  // CDM related stuff.

  // Owned by CDM which is external to this decoder.
  MediaCryptoContext* media_crypto_context_ = nullptr;

  // MediaDrmBridge requires registration/unregistration of the player, this
  // registration id is used for this.
  int cdm_registration_id_ = 0;

  // Do we need a hw-secure codec?
  bool requires_secure_codec_ = false;

  bool using_async_api_ = false;

  // Optional crypto object from the Cdm.
  base::android::ScopedJavaGlobalRef<jobject> media_crypto_;

  base::WeakPtrFactory<MediaCodecVideoDecoder> weak_factory_;
  base::WeakPtrFactory<MediaCodecVideoDecoder> codec_allocator_weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(MediaCodecVideoDecoder);
};

}  // namespace media

namespace std {

// Specialize std::default_delete to call Destroy().
template <>
struct MEDIA_GPU_EXPORT default_delete<media::MediaCodecVideoDecoder>
    : public default_delete<media::VideoDecoder> {};

}  // namespace std

#endif  // MEDIA_GPU_ANDROID_MEDIA_CODEC_VIDEO_DECODER_H_
