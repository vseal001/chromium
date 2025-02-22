// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMECAST_MEDIA_AUDIO_CAST_AUDIO_MANAGER_H_
#define CHROMECAST_MEDIA_AUDIO_CAST_AUDIO_MANAGER_H_

#include <memory>
#include <string>

#include "base/macros.h"
#include "base/single_thread_task_runner.h"
#include "media/audio/audio_manager_base.h"
#include "services/service_manager/public/cpp/connector.h"

namespace chromecast {

namespace media {

class CastAudioMixer;
class CmaBackendFactory;

class CastAudioManager : public ::media::AudioManagerBase {
 public:
  CastAudioManager(
      std::unique_ptr<::media::AudioThread> audio_thread,
      ::media::AudioLogFactory* audio_log_factory,
      base::RepeatingCallback<CmaBackendFactory*()> backend_factory_getter,
      scoped_refptr<base::SingleThreadTaskRunner> browser_task_runner,
      scoped_refptr<base::SingleThreadTaskRunner> backend_task_runner,
      bool use_mixer);
  ~CastAudioManager() override;

  // AudioManagerBase implementation.
  bool HasAudioOutputDevices() override;
  bool HasAudioInputDevices() override;
  void GetAudioInputDeviceNames(
      ::media::AudioDeviceNames* device_names) override;
  ::media::AudioParameters GetInputStreamParameters(
      const std::string& device_id) override;
  const char* GetName() override;
  void ReleaseOutputStream(::media::AudioOutputStream* stream) override;

  CmaBackendFactory* backend_factory();
  base::SingleThreadTaskRunner* backend_task_runner() {
    return backend_task_runner_.get();
  }

  void SetBrowserConnectorForTesting(
      service_manager::Connector* browser_connector);

 protected:
  // AudioManagerBase implementation.
  ::media::AudioOutputStream* MakeLinearOutputStream(
      const ::media::AudioParameters& params,
      const ::media::AudioManager::LogCallback& log_callback) override;
  ::media::AudioOutputStream* MakeLowLatencyOutputStream(
      const ::media::AudioParameters& params,
      const std::string& device_id,
      const ::media::AudioManager::LogCallback& log_callback) override;
  ::media::AudioInputStream* MakeLinearInputStream(
      const ::media::AudioParameters& params,
      const std::string& device_id,
      const ::media::AudioManager::LogCallback& log_callback) override;
  ::media::AudioInputStream* MakeLowLatencyInputStream(
      const ::media::AudioParameters& params,
      const std::string& device_id,
      const ::media::AudioManager::LogCallback& log_callback) override;
  ::media::AudioParameters GetPreferredOutputStreamParameters(
      const std::string& output_device_id,
      const ::media::AudioParameters& input_params) override;

  // Generates a CastAudioOutputStream for |mixer_|.
  virtual ::media::AudioOutputStream* MakeMixerOutputStream(
      const ::media::AudioParameters& params);

 private:
  friend class CastAudioMixer;

  base::RepeatingCallback<CmaBackendFactory*()> backend_factory_getter_;
  CmaBackendFactory* backend_factory_ = nullptr;
  scoped_refptr<base::SingleThreadTaskRunner> browser_task_runner_;
  scoped_refptr<base::SingleThreadTaskRunner> backend_task_runner_;
  std::unique_ptr<::media::AudioOutputStream> mixer_output_stream_;
  std::unique_ptr<CastAudioMixer> mixer_;

  // Used in tests to override the default browser connector.
  service_manager::Connector* browser_connector_ = nullptr;

  DISALLOW_COPY_AND_ASSIGN(CastAudioManager);
};

}  // namespace media
}  // namespace chromecast

#endif  // CHROMECAST_MEDIA_AUDIO_CAST_AUDIO_MANAGER_H_
