#ifndef TALK_APP_WEBRTC_JAVA_JNI_JNI_APP_AUDIO_DEVICE_H
#define TALK_APP_WEBRTC_JAVA_JNI_JNI_APP_AUDIO_DEVICE_H

#define WEBRTC_INCLUDE_INTERNAL_AUDIO_DEVICE

#include <jni.h>
// This is required with some JDKs to make the JNI exports visible, that is,
// expose symbols to the JVM for registering native method implementations.
// see: http://mail.openjdk.java.net/pipermail/core-libs-dev/2013-February/014446.html
#undef JNIEXPORT
#define JNIEXPORT __attribute__((visibility("default")))

#include "talk/app/webrtc/java/jni/classreferenceholder.h"
#include "talk/app/webrtc/java/jni/jni_helpers.h"
#include "talk/app/webrtc/java/jni/native_handle_impl.h"
#include "webrtc/base/logging.h"
#include "webrtc/base/scoped_ref_ptr.h"
#include "webrtc/base/thread_checker.h"
#include "webrtc/common_types.h"
#include "webrtc/common_audio/signal_processing/include/signal_processing_library.h"
#include "webrtc/modules/audio_device/audio_device_impl.h"
#include "webrtc/modules/audio_device/audio_device_generic.h"
#include "webrtc/system_wrappers/include/critical_section_wrapper.h"
#include "webrtc/system_wrappers/include/ref_count.h"
#include "webrtc/system_wrappers/include/tick_util.h"
#include "webrtc/system_wrappers/include/trace.h"

#undef WEBRTC_TRACE
#if defined(WEBRTC_RESTRICT_LOGGING)
// Disable all TRACE macros. The LOG macro is still functional.
#define WEBRTC_TRACE true ? (void) 0 : webrtc::Trace::Add
#else
#define WEBRTC_TRACE webrtc::Trace::Add
#endif

using webrtc::AudioDeviceModule;
using webrtc::AudioDeviceGeneric;
using webrtc::AudioDeviceModuleImpl;
using webrtc::CriticalSectionWrapper;
using webrtc::TickTime;
using webrtc::RefCountImpl;
using webrtc::AudioDeviceBuffer;
using webrtc::AudioDeviceObserver;
using webrtc::AudioTransport;
using webrtc::kAdmMaxDeviceNameSize;
using webrtc::kAdmMaxGuidSize;
using webrtc::kAdmMaxDeviceNameSize;
using webrtc::kAdmMaxGuidSize;
using webrtc::kAdmMaxFileNameSize;

namespace webrtc_jni {

    class JavaAppAudioDevice : public AudioDeviceGeneric {
    public:
        JavaAppAudioDevice(const int32_t id, JNIEnv* jni, jobject j_device);
        virtual ~JavaAppAudioDevice();

        // Retrieve the currently utilized audio layer.
        int32_t ActiveAudioLayer(
                AudioDeviceModule::AudioLayer& audioLayer) const override;

        // Main initialization and termination.
        int32_t Init() override;
        int32_t Terminate() override;
        bool Initialized() const override;

        // Device enumeration.
        int16_t PlayoutDevices() override;
        int16_t RecordingDevices() override;
        int32_t PlayoutDeviceName(uint16_t index,
                                  char name[kAdmMaxDeviceNameSize],
                                  char guid[kAdmMaxGuidSize]) override;
        int32_t RecordingDeviceName(uint16_t index,
                                    char name[kAdmMaxDeviceNameSize],
                                    char guid[kAdmMaxGuidSize]) override;

        // Device selection.
        int32_t SetPlayoutDevice(uint16_t index) override;
        int32_t SetPlayoutDevice(
                AudioDeviceModule::WindowsDeviceType device) override;
        int32_t SetRecordingDevice(uint16_t index) override;
        int32_t SetRecordingDevice(
                AudioDeviceModule::WindowsDeviceType device) override;

        // Audio transport initialization.
        int32_t PlayoutIsAvailable(bool& available) override;
        int32_t InitPlayout() override;
        bool PlayoutIsInitialized() const override;
        int32_t RecordingIsAvailable(bool& available) override;
        int32_t InitRecording() override;
        bool RecordingIsInitialized() const override;

        // Audio transport control.
        int32_t StartPlayout() override;
        int32_t StopPlayout() override;
        bool Playing() const override;
        int32_t StartRecording() override;
        int32_t StopRecording() override;
        bool Recording() const override;

        // Microphone Automatic Gain Control (AGC).
        int32_t SetAGC(bool enable) override;
        bool AGC() const override;

        // Volume control based on the Windows Wave API (Windows only).
        int32_t SetWaveOutVolume(uint16_t volumeLeft,
                                 uint16_t volumeRight) override;
        int32_t WaveOutVolume(uint16_t& volumeLeft,
                              uint16_t& volumeRight) const override;

        // Audio mixer initialization.
        int32_t InitSpeaker() override;
        bool SpeakerIsInitialized() const override;
        int32_t InitMicrophone() override;
        bool MicrophoneIsInitialized() const override;

        // Speaker volume controls.
        int32_t SpeakerVolumeIsAvailable(bool& available) override;
        int32_t SetSpeakerVolume(uint32_t volume) override;
        int32_t SpeakerVolume(uint32_t& volume) const override;
        int32_t MaxSpeakerVolume(uint32_t& maxVolume) const override;
        int32_t MinSpeakerVolume(uint32_t& minVolume) const override;
        int32_t SpeakerVolumeStepSize(uint16_t& stepSize) const override;

        // Microphone volume controls.
        int32_t MicrophoneVolumeIsAvailable(bool& available) override;
        int32_t SetMicrophoneVolume(uint32_t volume) override;
        int32_t MicrophoneVolume(uint32_t& volume) const override;
        int32_t MaxMicrophoneVolume(uint32_t& maxVolume) const override;
        int32_t MinMicrophoneVolume(uint32_t& minVolume) const override;
        int32_t MicrophoneVolumeStepSize(uint16_t& stepSize) const override;

        // Speaker mute control.
        int32_t SpeakerMuteIsAvailable(bool& available) override;
        int32_t SetSpeakerMute(bool enable) override;
        int32_t SpeakerMute(bool& enabled) const override;

        // Microphone mute control.
        int32_t MicrophoneMuteIsAvailable(bool& available) override;
        int32_t SetMicrophoneMute(bool enable) override;
        int32_t MicrophoneMute(bool& enabled) const override;

        // Microphone boost control.
        int32_t MicrophoneBoostIsAvailable(bool& available) override;
        int32_t SetMicrophoneBoost(bool enable) override;
        int32_t MicrophoneBoost(bool& enabled) const override;

        // Stereo support.
        int32_t StereoPlayoutIsAvailable(bool& available) override;
        int32_t SetStereoPlayout(bool enable) override;
        int32_t StereoPlayout(bool& enabled) const override;
        int32_t StereoRecordingIsAvailable(bool& available) override;
        int32_t SetStereoRecording(bool enable) override;
        int32_t StereoRecording(bool& enabled) const override;

        // Delay information and control.
        int32_t SetPlayoutBuffer(const AudioDeviceModule::BufferType type,
                                 uint16_t sizeMS = 0) override;
        int32_t PlayoutBuffer(AudioDeviceModule::BufferType& type,
                              uint16_t& sizeMS) const override;
        int32_t PlayoutDelay(uint16_t& delayMS) const override;
        int32_t RecordingDelay(uint16_t& delayMS) const override;

        // CPU load.
        int32_t CPULoad(uint16_t& load) const override;

        // Native sample rate controls (samples/sec)
        int32_t SetRecordingSampleRate(const uint32_t samplesPerSec) override;
        int32_t SetPlayoutSampleRate(const uint32_t samplesPerSec) override;

        // Playout & recording status.
        bool PlayoutWarning() const override;
        bool PlayoutError() const override;
        bool RecordingWarning() const override;
        bool RecordingError() const override;
        void ClearPlayoutWarning() override;
        void ClearPlayoutError() override;
        void ClearRecordingWarning() override;
        void ClearRecordingError() override;

        // Attaches an audio buffer to this device.
        void AttachAudioBuffer(AudioDeviceBuffer* audioBuffer) override;

        // The Java based AppAudioDeviceModule object calls this when recording has
        // started. Each call indicates that the 'recording_data_' audio buffer
        // (which is exposed to the object as a private direct ByteBuffer field) has
        // been filled with recorded samples and it is now time to send these to the
        // consumer.
        void DataIsRecorded();

        // The Java based AppAudioDeviceModule object calls this when playout has
        // started. Each call indicates that new bytes should be written for playout
        // to the 'playout_data_' audio buffer (which is exposed to the object as a
        // private direct ByteBuffer field).
        void GetPlayoutData();

    private:

        JNIEnv* jni() const { return AttachCurrentThreadIfNeeded(); }

        int32_t Available(bool& available, jmethodID methodID);

        int32_t Delay(uint16_t& delayMS, jmethodID methodID) const;

        void SetupBuffer(const uint32_t samplesPerSec,
                         int channels,
                         size_t* dataSize,
                         uint8_t** data,
                         uint32_t* framesPerBuffer,
                         jfieldID bufferField);

        CriticalSectionWrapper& _critSect;

        // Use ThreadChecker::CalledOnValidThread() to ensure that methods
        // inherited from AudioDeviceGeneric are called from the same thread.
        rtc::ThreadChecker thread_checker_;

        int32_t _id;
        bool _AGC;

        uint8_t _recChannels;
        uint8_t _playChannels;

        uint8_t* recording_data_;
        uint8_t* playout_data_;

        size_t recording_data_size_;
        size_t playout_data_size_;

        uint32_t rec_frames_per_buffer_;
        uint32_t play_frames_per_buffer_;

        ScopedGlobalRef<jobject> j_device_;

        jmethodID j_init_id_;
        jmethodID j_terminate_id_;
        jmethodID j_initialized_id_;

        jmethodID j_playout_is_available_id_;
        jmethodID j_init_playout_id_;
        jmethodID j_playout_is_initialized_id_;
        jmethodID j_recording_is_available_id_;
        jmethodID j_init_recording_id_;
        jmethodID j_recording_is_initialized_id_;

        jmethodID j_start_playout_id_;
        jmethodID j_stop_playout_id_;
        jmethodID j_playing_id_;
        jmethodID j_start_recording_id_;
        jmethodID j_stop_recording_id_;
        jmethodID j_recording_id_;

        jmethodID j_stereo_playout_is_available_id_;
        jmethodID j_stereo_recording_is_available_id_;

        jmethodID j_playout_delay_id_;
        jmethodID j_recording_delay_id_;

        jmethodID j_playout_warning_id_;
        jmethodID j_playout_error_id_;
        jmethodID j_recording_warning_id_;
        jmethodID j_recording_error_id_;
        jmethodID j_clear_playout_warning_id_;
        jmethodID j_clear_playout_error_id_;
        jmethodID j_clear_recording_warning_id_;
        jmethodID j_clear_recording_error_id_;

        jfieldID j_recording_buffer_id_;
        jfieldID j_playout_buffer_id_;

        // Raw pointer handle provided to us in AttachAudioBuffer(). Owned by the
        // AudioDeviceModuleImpl class and called by AudioDeviceModuleImpl::Create().
        // The AudioDeviceBuffer is a member of the AudioDeviceModuleImpl instance
        // and therefore outlives this object.
        AudioDeviceBuffer* audio_device_buffer_;

    };

    // JavaAppAudioDeviceModule.
    class JavaAppAudioDeviceModule : public AudioDeviceModuleImpl {
    public:
        JavaAppAudioDeviceModule(const int32_t id, JavaAppAudioDevice* device);
        static JavaAppAudioDeviceModule* Create(const int32_t id, JavaAppAudioDevice* device);
        JavaAppAudioDevice* device() { return device_; }
    private:
        JavaAppAudioDevice* device_;
    };

    // JNI exports.
    JOW(jlong, AppAudioDeviceModule_nativeWrapAppAudioDeviceModule)(
            JNIEnv* jni, jclass, jobject j_module);

    JOW(void, AppAudioDeviceModule_nativeSetRecordingSampleRate)(
            JNIEnv* jni, jclass clazz, jlong nativeModule, jlong sampleRate);

    JOW(void, AppAudioDeviceModule_nativeSetPlayoutSampleRate)(
            JNIEnv* jni, jclass clazz, jlong nativeModule, jlong sampleRate);

    JOW(void, AppAudioDeviceModule_nativeDataIsRecorded)(
            JNIEnv* jni, jclass clazz, jlong nativeModule);

    JOW(void, AppAudioDeviceModule_nativeGetPlayoutData)(
            JNIEnv* jni, jclass clazz, jlong nativeModule);

}

#endif //TALK_APP_WEBRTC_JAVA_JNI_JNI_APP_AUDIO_DEVICE_H
