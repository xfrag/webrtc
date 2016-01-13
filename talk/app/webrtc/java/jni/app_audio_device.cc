#include "app_audio_device.h"

using webrtc::AudioDeviceModule;
using webrtc::AudioDeviceGeneric;
using webrtc::AudioDeviceModuleImpl;
using webrtc::CriticalSectionScoped;
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
using webrtc::kTraceMemory;
using webrtc::kTraceAudioDevice;
using webrtc::kTraceError;
using webrtc::kTraceWarning;

namespace webrtc_jni {

    // Audio data format is PCM 16 bit per sample.
    static const int BITS_PER_SAMPLE = 16;

    // Requested size of each recorded buffer provided to the client.
    static const int CALLBACK_BUFFER_SIZE_MS = 10;

    // Average number of callbacks per second.
    static const int BUFFERS_PER_SECOND = 1000 / CALLBACK_BUFFER_SIZE_MS;

    // Took inspiration from the Android implementation.
    // See: "src/webrtc/modules/audio_device/android/".
    // "audio_device_template.h" is a good starting point.

    JavaAppAudioDevice::JavaAppAudioDevice(const int32_t id, JNIEnv *jni, jobject j_device)
            : _critSect(*CriticalSectionWrapper::CreateCriticalSection()),
              _id(id),
              _AGC(false),
              _recChannels(1),
              _playChannels(1),
              recording_data_(NULL),
              playout_data_(NULL),
              recording_data_size_(0),
              playout_data_size_(0),
              j_device_(jni, j_device),
            // ------------------------------------------------------------------ //
              j_init_id_(GetMethodID(
                      jni, GetObjectClass(jni, j_device), "init",
                      "()I")),
              j_terminate_id_(GetMethodID(
                      jni, GetObjectClass(jni, j_device), "terminate",
                      "()I")),
              j_initialized_id_(GetMethodID(
                      jni, GetObjectClass(jni, j_device), "initialized",
                      "()Z")),
            // ------------------------------------------------------------------ //
              j_playout_is_available_id_(GetMethodID(
                      jni, GetObjectClass(jni, j_device), "playoutIsAvailable",
                      "()I")),
              j_init_playout_id_(GetMethodID(
                      jni, GetObjectClass(jni, j_device), "initPlayout",
                      "()I")),
              j_playout_is_initialized_id_(GetMethodID(
                      jni, GetObjectClass(jni, j_device), "playoutIsInitialized",
                      "()Z")),
              j_recording_is_available_id_(GetMethodID(
                      jni, GetObjectClass(jni, j_device), "recordingIsAvailable",
                      "()I")),
              j_init_recording_id_(GetMethodID(
                      jni, GetObjectClass(jni, j_device), "initRecording",
                      "()I")),
              j_recording_is_initialized_id_(GetMethodID(
                      jni, GetObjectClass(jni, j_device), "recordingIsInitialized",
                      "()Z")),
            // ------------------------------------------------------------------ //
              j_start_playout_id_(GetMethodID(
                      jni, GetObjectClass(jni, j_device), "startPlayout",
                      "()I")),
              j_stop_playout_id_(GetMethodID(
                      jni, GetObjectClass(jni, j_device), "stopPlayout",
                      "()I")),
              j_playing_id_(GetMethodID(
                      jni, GetObjectClass(jni, j_device), "playing",
                      "()Z")),
              j_start_recording_id_(GetMethodID(
                      jni, GetObjectClass(jni, j_device), "startRecording",
                      "()I")),
              j_stop_recording_id_(GetMethodID(
                      jni, GetObjectClass(jni, j_device), "stopRecording",
                      "()I")),
              j_recording_id_(GetMethodID(
                      jni, GetObjectClass(jni, j_device), "recording",
                      "()Z")),
            // ------------------------------------------------------------------ //
              j_stereo_playout_is_available_id_(GetMethodID(
                      jni, GetObjectClass(jni, j_device), "stereoPlayoutIsAvailable",
                      "()Z")),
              j_stereo_recording_is_available_id_(GetMethodID(
                      jni, GetObjectClass(jni, j_device), "stereoRecordingIsAvailable",
                      "()Z")),
            // ------------------------------------------------------------------ //
              j_playout_delay_id_(GetMethodID(
                      jni, GetObjectClass(jni, j_device), "playoutDelay",
                      "()I")),
              j_recording_delay_id_(GetMethodID(
                      jni, GetObjectClass(jni, j_device), "recordingDelay",
                      "()I")),
            // ------------------------------------------------------------------ //
              j_playout_warning_id_(GetMethodID(
                      jni, GetObjectClass(jni, j_device), "playoutWarning",
                      "()Z")),
              j_playout_error_id_(GetMethodID(
                      jni, GetObjectClass(jni, j_device), "playoutError",
                      "()Z")),
              j_recording_warning_id_(GetMethodID(
                      jni, GetObjectClass(jni, j_device), "recordingWarning",
                      "()Z")),
              j_recording_error_id_(GetMethodID(
                      jni, GetObjectClass(jni, j_device), "recordingError",
                      "()Z")),
              j_clear_playout_warning_id_(GetMethodID(
                      jni, GetObjectClass(jni, j_device), "clearPlayoutWarning",
                      "()V")),
              j_clear_playout_error_id_(GetMethodID(
                      jni, GetObjectClass(jni, j_device), "clearPlayoutError",
                      "()V")),
              j_clear_recording_warning_id_(GetMethodID(
                      jni, GetObjectClass(jni, j_device), "clearRecordingWarning",
                      "()V")),
              j_clear_recording_error_id_(GetMethodID(
                      jni, GetObjectClass(jni, j_device), "clearRecordingError",
                      "()V")),
            // ------------------------------------------------------------------ //
              j_recording_buffer_id_(GetFieldID(
                      jni, GetObjectClass(jni, j_device), "recordingBuffer",
                      "Ljava/nio/ByteBuffer;")),
              j_playout_buffer_id_(GetFieldID(
                      jni, GetObjectClass(jni, j_device), "playoutBuffer",
                      "Ljava/nio/ByteBuffer;")) {

        CHECK_EXCEPTION(jni);

        // Detach from the calling thread. To be attached later to the first thread
        // that triggers an RTC_DCHECK(thread_checker_.CalledOnValidThread()).
        thread_checker_.DetachFromThread();

    }

    JavaAppAudioDevice::~JavaAppAudioDevice() {

        LOG(LS_INFO) << "JavaAppAudioDevice::~JavaAppAudioDevice()";

        // The destructor is normally called from a Java thread executing
        // AppAudioDeviceModule.dispose(). Detach the thread checker to allow
        // execution of the Terminate() method.
        thread_checker_.DetachFromThread();
        Terminate();

        if (playout_data_) {
            delete [] playout_data_;
            playout_data_ = NULL;
        }
        if (recording_data_) {
            delete [] recording_data_;
            recording_data_ = NULL;
        }

        delete &_critSect;

    }

    // Retrieve the currently utilized audio layer
    int32_t JavaAppAudioDevice::ActiveAudioLayer(
            AudioDeviceModule::AudioLayer &audioLayer) const {
        // Not sure if this is the best choice. Other options are "kDummyAudio",
        // NULL, or leave output as is. Seems like it has no impact anyway.
        audioLayer = AudioDeviceModule::kPlatformDefaultAudio;
        return 0;
    }

// ============================================================================
//                       Main initialization and termination.
// ============================================================================

    int32_t JavaAppAudioDevice::Init() {
        LOG(LS_INFO) << "JavaAppAudioDevice::Init called";
        RTC_DCHECK(thread_checker_.CalledOnValidThread());
        ScopedLocalRefFrame local_ref_frame(jni());
        jint result = jni()->CallIntMethod(*j_device_, j_init_id_);
        CHECK_EXCEPTION(jni());
        return result;
    }

    int32_t JavaAppAudioDevice::Terminate() {
        LOG(LS_INFO) << "JavaAppAudioDevice::Terminate called";
        RTC_DCHECK(thread_checker_.CalledOnValidThread());
        ScopedLocalRefFrame local_ref_frame(jni());
        jint result = jni()->CallIntMethod(*j_device_, j_terminate_id_);
        CHECK_EXCEPTION(jni());
        return result;
    }

    bool JavaAppAudioDevice::Initialized() const {
        LOG(LS_INFO) << "JavaAppAudioDevice::Initialized called";
        RTC_DCHECK(thread_checker_.CalledOnValidThread());
        ScopedLocalRefFrame local_ref_frame(jni());
        jboolean result = jni()->CallBooleanMethod(*j_device_, j_initialized_id_);
        CHECK_EXCEPTION(jni());
        return result;
    }

// ============================================================================
//                              Device enumeration.
// ============================================================================

    // Report a single device here and let the Java AppAudioDeviceModule to
    // select whatever device is appropriate.

    int16_t JavaAppAudioDevice::PlayoutDevices() {
        LOG(LS_INFO) << "JavaAppAudioDevice::PlayoutDevices called";
        return 1;
    }

    int16_t JavaAppAudioDevice::RecordingDevices() {
        LOG(LS_INFO) << "JavaAppAudioDevice::RecordingDevices called";
        return 1;
    }

    // After a search for usages, looks like the device name functions are never
    // called, with the exception of some references in tests & tools. So it must
    // be safe to omit support here.

    int32_t JavaAppAudioDevice::PlayoutDeviceName(uint16_t /*index*/,
                                                  char /*name*/[kAdmMaxDeviceNameSize],
                                                  char /*guid*/[kAdmMaxGuidSize]) {
        WEBRTC_TRACE(kTraceError, kTraceAudioDevice, _id,
                     "  PlayoutDeviceName not supported");
        return -1;
    }

    int32_t JavaAppAudioDevice::RecordingDeviceName(uint16_t /*index*/,
                                                    char /*name*/[kAdmMaxDeviceNameSize],
                                                    char /*guid*/[kAdmMaxGuidSize]) {
        WEBRTC_TRACE(kTraceError, kTraceAudioDevice, _id,
                     "  PlayoutDeviceName not supported");
        return -1;
    }

// ============================================================================
//                              Device selection.
// ============================================================================

    // Device selection has no effect since it is up to the Java application to
    // do this.

    int32_t JavaAppAudioDevice::SetPlayoutDevice(uint16_t index) {
        LOG(LS_INFO) << "JavaAppAudioDevice::SetPlayoutDevice called. (index=" << index << ")";
        return 0;
    }

    int32_t JavaAppAudioDevice::SetPlayoutDevice(
            AudioDeviceModule::WindowsDeviceType /*device*/) {
        WEBRTC_TRACE(kTraceError, kTraceAudioDevice, _id,
                     "  WindowsDeviceType not supported");
        return -1;
    }

    int32_t JavaAppAudioDevice::SetRecordingDevice(uint16_t index) {
        LOG(LS_INFO) << "JavaAppAudioDevice::SetRecordingDevice. (index=" << index << ")";
        return 0;
    }

    int32_t JavaAppAudioDevice::SetRecordingDevice(
            AudioDeviceModule::WindowsDeviceType /*device*/) {
        WEBRTC_TRACE(kTraceError, kTraceAudioDevice, _id,
                     "WindowsDeviceType not supported");
        return -1;
    }

// ============================================================================
//                       Audio transport initialization.
// ============================================================================

    int32_t JavaAppAudioDevice::PlayoutIsAvailable(bool& available) {
        LOG(LS_INFO) << "JavaAppAudioDevice::PlayoutIsAvailable called";
        RTC_DCHECK(thread_checker_.CalledOnValidThread());
        return Available(available, j_playout_is_available_id_);
    }

    int32_t JavaAppAudioDevice::InitPlayout() {
        LOG(LS_INFO) << "JavaAppAudioDevice::InitPlayout called";
        RTC_DCHECK(thread_checker_.CalledOnValidThread());
        ScopedLocalRefFrame local_ref_frame(jni());
        jint result = jni()->CallIntMethod(*j_device_, j_init_playout_id_);
        CHECK_EXCEPTION(jni());
        return result;
    }

    bool JavaAppAudioDevice::PlayoutIsInitialized() const {
        LOG(LS_INFO) << "JavaAppAudioDevice::PlayoutIsInitialized called";
        RTC_DCHECK(thread_checker_.CalledOnValidThread());
        ScopedLocalRefFrame local_ref_frame(jni());
        jboolean result = jni()->CallBooleanMethod(*j_device_, j_playout_is_initialized_id_);
        CHECK_EXCEPTION(jni());
        return result;
    }

    int32_t JavaAppAudioDevice::RecordingIsAvailable(bool& available) {
        LOG(LS_INFO) << "JavaAppAudioDevice::RecordingIsAvailable called";
        RTC_DCHECK(thread_checker_.CalledOnValidThread());
        return Available(available, j_recording_is_available_id_);
    }

    int32_t JavaAppAudioDevice::InitRecording() {
        LOG(LS_INFO) << "JavaAppAudioDevice::InitRecording called";
        RTC_DCHECK(thread_checker_.CalledOnValidThread());
        ScopedLocalRefFrame local_ref_frame(jni());
        jint result = jni()->CallIntMethod(*j_device_, j_init_recording_id_);
        CHECK_EXCEPTION(jni());
        return result;
    }

    bool JavaAppAudioDevice::RecordingIsInitialized() const {
        LOG(LS_INFO) << "JavaAppAudioDevice::RecordingIsInitialized called";
        RTC_DCHECK(thread_checker_.CalledOnValidThread());
        ScopedLocalRefFrame local_ref_frame(jni());
        jboolean result = jni()->CallBooleanMethod(*j_device_, j_recording_is_initialized_id_);
        CHECK_EXCEPTION(jni());
        return result;
    }

// ============================================================================
//                           Audio transport control.
// ============================================================================

    int32_t JavaAppAudioDevice::StartPlayout() {
        LOG(LS_INFO) << "JavaAppAudioDevice::StartPlayout called";
        RTC_DCHECK(thread_checker_.CalledOnValidThread());
        ScopedLocalRefFrame local_ref_frame(jni());
        jint result = jni()->CallIntMethod(*j_device_, j_start_playout_id_);
        CHECK_EXCEPTION(jni());
        return result;
    }

    int32_t JavaAppAudioDevice::StopPlayout() {
        LOG(LS_INFO) << "JavaAppAudioDevice::StopPlayout called";
        RTC_DCHECK(thread_checker_.CalledOnValidThread());
        ScopedLocalRefFrame local_ref_frame(jni());
        jint result = jni()->CallIntMethod(*j_device_, j_stop_playout_id_);
        CHECK_EXCEPTION(jni());
        return result;
    }

    bool JavaAppAudioDevice::Playing() const {
        LOG(LS_INFO) << "JavaAppAudioDevice::Playing called";
        RTC_DCHECK(thread_checker_.CalledOnValidThread());
        ScopedLocalRefFrame local_ref_frame(jni());
        jboolean result = jni()->CallBooleanMethod(*j_device_, j_playing_id_);
        CHECK_EXCEPTION(jni());
        return result;
    }

    int32_t JavaAppAudioDevice::StartRecording() {
        LOG(LS_INFO) << "JavaAppAudioDevice::StartRecording called";
        RTC_DCHECK(thread_checker_.CalledOnValidThread());
        ScopedLocalRefFrame local_ref_frame(jni());
        jint result = jni()->CallIntMethod(*j_device_, j_start_recording_id_);
        CHECK_EXCEPTION(jni());
        return result;
    }

    int32_t JavaAppAudioDevice::StopRecording() {
        LOG(LS_INFO) << "JavaAppAudioDevice::StopRecording called";
        RTC_DCHECK(thread_checker_.CalledOnValidThread());
        ScopedLocalRefFrame local_ref_frame(jni());
        jint result = jni()->CallIntMethod(*j_device_, j_stop_recording_id_);
        CHECK_EXCEPTION(jni());
        return result;
    }

    bool JavaAppAudioDevice::Recording() const {
        LOG(LS_INFO) << "JavaAppAudioDevice::Recording called";
        RTC_DCHECK(thread_checker_.CalledOnValidThread());
        ScopedLocalRefFrame local_ref_frame(jni());
        jboolean result = jni()->CallBooleanMethod(*j_device_, j_recording_id_);
        CHECK_EXCEPTION(jni());
        return result;
    }

// ============================================================================
//                    Microphone Automatic Gain Control (AGC).
// ============================================================================

    // The implementations below were copied from audio_device_pulse_linux.cc.
    // It looks like there is no point for exposing this to the Java application.

    int32_t JavaAppAudioDevice::SetAGC(bool enable) {
        CriticalSectionScoped lock(&_critSect);
        _AGC = enable;
        return 0;
    }

    bool JavaAppAudioDevice::AGC() const {
        CriticalSectionScoped lock(&_critSect);
        return _AGC;
    }

// ============================================================================
//         Volume control based on the Windows Wave API (Windows only).
// ============================================================================

    int32_t JavaAppAudioDevice::SetWaveOutVolume(uint16_t /*volumeLeft*/,
                                                 uint16_t /*volumeRight*/) {

        WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, _id,
                     "  SetWaveOutVolume not supported");
        return -1;
    }

    int32_t JavaAppAudioDevice::WaveOutVolume(uint16_t& /*volumeLeft*/,
                                              uint16_t& /*volumeRight*/) const {

        WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, _id,
                     "  WaveOutVolume not supported");
        return -1;
    }

// ============================================================================
//                         Audio mixer initialization.
// ============================================================================

    // The mixer part of the API is not exposed to the Java API. Mixer features
    // such as volume/mute/stereo are reported as not available in this version
    // in order to keep things simple.

    int32_t JavaAppAudioDevice::InitSpeaker() {
        LOG(LS_INFO) << "JavaAppAudioDevice::InitSpeaker called";
        return 0;
    }

    bool JavaAppAudioDevice::SpeakerIsInitialized() const {
        LOG(LS_INFO) << "JavaAppAudioDevice::SpeakerIsInitialized called";
        return true;
    }

    int32_t JavaAppAudioDevice::InitMicrophone() {
        LOG(LS_INFO) << "JavaAppAudioDevice::InitMicrophone called";
        return 0;
    }

    bool JavaAppAudioDevice::MicrophoneIsInitialized() const {
        LOG(LS_INFO) << "JavaAppAudioDevice::MicrophoneIsInitialized called";
        return true;
    }

// ============================================================================
//                         Speaker volume controls.
// ============================================================================

    int32_t JavaAppAudioDevice::SpeakerVolumeIsAvailable(bool& available) {
        LOG(LS_INFO) << "JavaAppAudioDevice::SpeakerVolumeIsAvailable called";
        available = false;
        return 0;
    }

    int32_t JavaAppAudioDevice::SetSpeakerVolume(uint32_t /*volume*/) {
        WEBRTC_TRACE(kTraceError, kTraceAudioDevice, _id,
                     "  SetSpeakerVolume not supported");
        return -1;
    }

    int32_t JavaAppAudioDevice::SpeakerVolume(uint32_t& /*volume*/) const {
        WEBRTC_TRACE(kTraceError, kTraceAudioDevice, _id,
                     "  SpeakerVolume not supported");
        return -1;
    }

    int32_t JavaAppAudioDevice::MaxSpeakerVolume(uint32_t& /*maxVolume*/) const {
        WEBRTC_TRACE(kTraceError, kTraceAudioDevice, _id,
                     "  MaxSpeakerVolume not supported");
        return -1;
    }

    int32_t JavaAppAudioDevice::MinSpeakerVolume(uint32_t& /*minVolume*/) const {
        WEBRTC_TRACE(kTraceError, kTraceAudioDevice, _id,
                     "  MinSpeakerVolume not supported");
        return -1;
    }

    int32_t JavaAppAudioDevice::SpeakerVolumeStepSize(uint16_t& /*stepSize*/) const {
        WEBRTC_TRACE(kTraceError, kTraceAudioDevice, _id,
                     "  SpeakerVolumeStepSize not supported");
        return -1;
    }

// ============================================================================
//                         Microphone volume controls.
// ============================================================================

    int32_t JavaAppAudioDevice::MicrophoneVolumeIsAvailable(bool& available) {
        LOG(LS_INFO) << "JavaAppAudioDevice::MicrophoneVolumeIsAvailable called";
        available = false;
        return 0;
    }

    int32_t JavaAppAudioDevice::SetMicrophoneVolume(uint32_t /*volume*/) {
        WEBRTC_TRACE(kTraceError, kTraceAudioDevice, _id,
                     "  SetMicrophoneVolume not supported");
        return -1;
    }

    int32_t JavaAppAudioDevice::MicrophoneVolume(uint32_t& /*volume*/) const {
        WEBRTC_TRACE(kTraceError, kTraceAudioDevice, _id,
                     "  MicrophoneVolume not supported");
        return -1;
    }

    int32_t JavaAppAudioDevice::MaxMicrophoneVolume(uint32_t& /*maxVolume*/) const {
        WEBRTC_TRACE(kTraceError, kTraceAudioDevice, _id,
                     "  MaxMicrophoneVolume not supported");
        return -1;
    }

    int32_t JavaAppAudioDevice::MinMicrophoneVolume(uint32_t& /*minVolume*/) const {
        WEBRTC_TRACE(kTraceError, kTraceAudioDevice, _id,
                     "  MinMicrophoneVolume not supported");
        return -1;
    }

    int32_t JavaAppAudioDevice::MicrophoneVolumeStepSize(uint16_t& /*stepSize*/) const {
        WEBRTC_TRACE(kTraceError, kTraceAudioDevice, _id,
                     "  MicrophoneVolumeStepSize not supported");
        return -1;
    }

// ============================================================================
//                          Speaker mute control.
// ============================================================================

    int32_t JavaAppAudioDevice::SpeakerMuteIsAvailable(bool& available) {
        LOG(LS_INFO) << "JavaAppAudioDevice::SpeakerMuteIsAvailable called";
        available = false;
        return 0;
    }

    int32_t JavaAppAudioDevice::SetSpeakerMute(bool enable) {
        LOG(LS_INFO) << "JavaAppAudioDevice::SetSpeakerMute(" << enable << ") called";
        if (!enable) return 0;
        WEBRTC_TRACE(kTraceError, kTraceAudioDevice, _id,
                     "  SetSpeakerMute not supported");
        return -1;
    }

    int32_t JavaAppAudioDevice::SpeakerMute(bool& enabled) const {
        LOG(LS_INFO) << "JavaAppAudioDevice::SpeakerMute called";
        enabled = false;
        return 0;
    }

// ============================================================================
//                          Microphone mute control.
// ============================================================================

    int32_t JavaAppAudioDevice::MicrophoneMuteIsAvailable(bool& available) {
        LOG(LS_INFO) << "JavaAppAudioDevice::MicrophoneMuteIsAvailable called";
        available = false;
        return 0;
    }

    int32_t JavaAppAudioDevice::SetMicrophoneMute(bool enable) {
        LOG(LS_INFO) << "JavaAppAudioDevice::SetMicrophoneMute(" << enable << ") called";
        if (!enable) return 0;
        WEBRTC_TRACE(kTraceError, kTraceAudioDevice, _id,
                     "  SetMicrophoneMute not supported");
        return -1;
    }

    int32_t JavaAppAudioDevice::MicrophoneMute(bool& enabled) const {
        LOG(LS_INFO) << "JavaAppAudioDevice::MicrophoneMute called";
        enabled = false;
        return 0;
    }

// ============================================================================
//                          Microphone boost control.
// ============================================================================

    int32_t JavaAppAudioDevice::MicrophoneBoostIsAvailable(bool& available) {
        LOG(LS_INFO) << "JavaAppAudioDevice::MicrophoneBoostIsAvailable called";
        available = false;
        return 0;
    }

    int32_t JavaAppAudioDevice::SetMicrophoneBoost(bool enable) {
        LOG(LS_INFO) << "JavaAppAudioDevice::SetMicrophoneBoost(" << enable << ") called";
        if (!enable) return 0;
        WEBRTC_TRACE(kTraceError, kTraceAudioDevice, _id,
                     "  SetMicrophoneBoost not supported");
        return -1;
    }

    int32_t JavaAppAudioDevice::MicrophoneBoost(bool& enabled) const {
        LOG(LS_INFO) << "JavaAppAudioDevice::MicrophoneBoost called";
        enabled = false;
        return 0;
    }

// ============================================================================
//                              Stereo support.
// ============================================================================

    int32_t JavaAppAudioDevice::StereoPlayoutIsAvailable(bool& available) {

        LOG(LS_INFO) << "JavaAppAudioDevice::StereoPlayoutIsAvailable called";
        RTC_DCHECK(thread_checker_.CalledOnValidThread());
        ScopedLocalRefFrame local_ref_frame(jni());
        jboolean result = jni()->CallBooleanMethod(*j_device_, j_stereo_playout_is_available_id_);
        CHECK_EXCEPTION(jni());
        available = result;
        return 0;

    }

    // This is called with the result of StereoPlayoutIsAvailable() (see
    // voe_base_impl.cc:311). The implementation is copied from
    // audio_device_pulse_linux.cc. The same holds true for the rest of the
    // stereo support related methods below.
    int32_t JavaAppAudioDevice::SetStereoPlayout(bool enable) {

        LOG(LS_INFO) << "JavaAppAudioDevice::SetStereoPlayout(" << enable << ") called";
        if (enable) _playChannels = 2;
        else _playChannels = 1;
        return 0;

    }

    int32_t JavaAppAudioDevice::StereoPlayout(bool& enabled) const {

        LOG(LS_INFO) << "JavaAppAudioDevice::StereoPlayout called";
        RTC_DCHECK(thread_checker_.CalledOnValidThread());
        enabled = _playChannels == 2;
        return 0;

    }

    int32_t JavaAppAudioDevice::StereoRecordingIsAvailable(bool& available) {

        LOG(LS_INFO) << "JavaAppAudioDevice::StereoRecordingIsAvailable called";
        RTC_DCHECK(thread_checker_.CalledOnValidThread());
        ScopedLocalRefFrame local_ref_frame(jni());
        jboolean result = jni()->CallBooleanMethod(*j_device_, j_stereo_recording_is_available_id_);
        CHECK_EXCEPTION(jni());
        available = result;
        return 0;

    }

    int32_t JavaAppAudioDevice::SetStereoRecording(bool enable) {

        LOG(LS_INFO) << "JavaAppAudioDevice::SetStereoRecording(" << enable << ") called";
        RTC_DCHECK(thread_checker_.CalledOnValidThread());
        if (enable) _recChannels = 2;
        else _recChannels = 1;
        return 0;

    }

    int32_t JavaAppAudioDevice::StereoRecording(bool& enabled) const {

        LOG(LS_INFO) << "JavaAppAudioDevice::StereoRecording called";
        RTC_DCHECK(thread_checker_.CalledOnValidThread());
        enabled = _recChannels == 2;
        return 0;

    }

// ============================================================================
//                        Delay information and control.
// ============================================================================

    int32_t JavaAppAudioDevice::SetPlayoutBuffer(
            const AudioDeviceModule::BufferType type, uint16_t sizeMS) {
        WEBRTC_TRACE(kTraceError, kTraceAudioDevice, _id,
                     "  SetPlayoutBuffer not supported");
        return -1;
    }

    int32_t JavaAppAudioDevice::PlayoutBuffer(
            AudioDeviceModule::BufferType& type, uint16_t& sizeMS) const {
        WEBRTC_TRACE(kTraceError, kTraceAudioDevice, _id,
                     "  PlayoutBuffer not supported");
        return -1;
    }

    int32_t JavaAppAudioDevice::PlayoutDelay(uint16_t& delayMS) const {
        return Delay(delayMS, j_playout_delay_id_);
    }

    int32_t JavaAppAudioDevice::RecordingDelay(uint16_t& delayMS) const {
        return Delay(delayMS, j_recording_delay_id_);
    }

// ============================================================================
//                                 CPU load.
// ============================================================================

    int32_t JavaAppAudioDevice::CPULoad(uint16_t& load) const {
        WEBRTC_TRACE(kTraceError, kTraceAudioDevice, _id,
                     "  CPULoad not supported");
        return -1;
    }

// ============================================================================
//                    Native sample rate controls (samples/sec).
// ============================================================================

    // These methods setup the recording & playout device buffers to be used in
    // DataIsRecorded() and GetPlayoutData(). The AppAudioDeviceModule will call
    // them in response to initPlayout() and initRecording(), after querying the
    // effective sampling rates from its concrete implementation. The resulting
    // buffers are passed back to the Java AppAudioDeviceModule by setting its
    // 'recordingBuffer' and 'playoutBuffer' fields. Thread safety is controlled
    // in the Java side with the use of locks. Here, we only check if the calling
    // thread matches with the one that called InitPlayout() on this object.

    int32_t JavaAppAudioDevice::SetRecordingSampleRate(const uint32_t samplesPerSec) {
        LOG(LS_INFO) << "JavaAppAudioDevice::SetRecordingSampleRate called: " << samplesPerSec;
        RTC_DCHECK(thread_checker_.CalledOnValidThread());
        audio_device_buffer_->SetRecordingSampleRate(samplesPerSec);
        SetupBuffer(
                samplesPerSec,
                _recChannels,
                &recording_data_size_,
                &recording_data_,
                &rec_frames_per_buffer_,
                j_recording_buffer_id_
        );
        return 0;
    }

    int32_t JavaAppAudioDevice::SetPlayoutSampleRate(const uint32_t samplesPerSec) {
        LOG(LS_INFO) << "JavaAppAudioDevice::SetPlayoutSampleRate called: " << samplesPerSec;
        RTC_DCHECK(thread_checker_.CalledOnValidThread());
        audio_device_buffer_->SetPlayoutSampleRate(samplesPerSec);
        SetupBuffer(
                samplesPerSec,
                _playChannels,
                &playout_data_size_,
                &playout_data_,
                &play_frames_per_buffer_,
                j_playout_buffer_id_
        );
        return 0;
    }

// ============================================================================
//                          Playout & recording status.
// ============================================================================

    bool JavaAppAudioDevice::PlayoutWarning() const {
        ScopedLocalRefFrame local_ref_frame(jni());
        jboolean warning = jni()->CallBooleanMethod(*j_device_, j_playout_warning_id_);
        CHECK_EXCEPTION(jni());
        return warning;
    }

    bool JavaAppAudioDevice::PlayoutError() const {
        ScopedLocalRefFrame local_ref_frame(jni());
        jboolean error = jni()->CallBooleanMethod(*j_device_, j_playout_error_id_);
        CHECK_EXCEPTION(jni());
        return error;
    }

    bool JavaAppAudioDevice::RecordingWarning() const {
        ScopedLocalRefFrame local_ref_frame(jni());
        jboolean warning = jni()->CallBooleanMethod(*j_device_, j_recording_warning_id_);
        CHECK_EXCEPTION(jni());
        return warning;
    }

    bool JavaAppAudioDevice::RecordingError() const {
        ScopedLocalRefFrame local_ref_frame(jni());
        jboolean error = jni()->CallBooleanMethod(*j_device_, j_recording_error_id_);
        CHECK_EXCEPTION(jni());
        return error;
    }

    void JavaAppAudioDevice::ClearPlayoutWarning() {
        ScopedLocalRefFrame local_ref_frame(jni());
        jni()->CallVoidMethod(*j_device_, j_clear_playout_warning_id_);
        CHECK_EXCEPTION(jni());
    }

    void JavaAppAudioDevice::ClearPlayoutError() {
        ScopedLocalRefFrame local_ref_frame(jni());
        jni()->CallVoidMethod(*j_device_, j_clear_playout_error_id_);
        CHECK_EXCEPTION(jni());
    }

    void JavaAppAudioDevice::ClearRecordingWarning() {
        ScopedLocalRefFrame local_ref_frame(jni());
        jni()->CallVoidMethod(*j_device_, j_clear_recording_warning_id_);
        CHECK_EXCEPTION(jni());
    }

    void JavaAppAudioDevice::ClearRecordingError() {
        ScopedLocalRefFrame local_ref_frame(jni());
        jni()->CallVoidMethod(*j_device_, j_clear_recording_error_id_);
        CHECK_EXCEPTION(jni());
    }

// ============================================================================
//                       Audio Device Buffer.
// ============================================================================

    void JavaAppAudioDevice::AttachAudioBuffer(AudioDeviceBuffer* audioBuffer) {

        audio_device_buffer_ = audioBuffer;

        // Inform the AudioBuffer about default settings for this implementation.
        // Set all values to zero here since the actual settings should be done by
        // the wrapped Java implementation later.
        audio_device_buffer_->SetRecordingSampleRate(0);
        audio_device_buffer_->SetPlayoutSampleRate(0);

        audio_device_buffer_->SetRecordingChannels(1);
        audio_device_buffer_->SetPlayoutChannels(1);

    }

    // Thread safety and, in particular, access to the recording & playout device
    // buffers, is ensured by the Java AppAudioDeviceModule, where a lock prevents
    // concurrent access from multiple threads.

    // See AudioRecordJni::OnDataIsRecorded 
    // at webrtc/modules/audio_device/android/audio_record_jni.cc    
    void JavaAppAudioDevice::DataIsRecorded() {

        if (!audio_device_buffer_) {
            WEBRTC_TRACE(kTraceError, kTraceAudioDevice, _id,
                         "  AttachAudioBuffer has not been called!");
            return;
        }
        audio_device_buffer_->SetRecordedBuffer(recording_data_,
                                                rec_frames_per_buffer_);

        uint16_t playDelay(0);
        if (PlayoutDelay(playDelay) == -1) {
            WEBRTC_TRACE(kTraceError, kTraceAudioDevice, _id,
                         "  failed to retrieve the playout delay");
            return;
        }

        uint16_t recDelay(0);
        if (RecordingDelay(recDelay) == -1) {
            WEBRTC_TRACE(kTraceError, kTraceAudioDevice, _id,
                         "  failed to retrieve the recording delay");
            return;
        }

        audio_device_buffer_->SetVQEData(playDelay, recDelay, 0);
        if (audio_device_buffer_->DeliverRecordedData() == -1) {
            WEBRTC_TRACE(kTraceError, kTraceAudioDevice, _id,
                         "  AudioDeviceBuffer::DeliverRecordedData failed!");
        }

    }

    // See AudioTrackJni::OnGetPlayoutData 
    // at webrtc/modules/audio_device/android/audio_track_jni.cc
    void JavaAppAudioDevice::GetPlayoutData() {

        if (!audio_device_buffer_) {
            WEBRTC_TRACE(kTraceError, kTraceAudioDevice, _id,
                         "  AttachAudioBuffer has not been called!");
            return;
        }
        // Pull decoded data (in 16-bit PCM format) from jitter buffer.
        int samples = audio_device_buffer_->RequestPlayoutData(play_frames_per_buffer_);
        if (samples <= 0) {
            WEBRTC_TRACE(kTraceError, kTraceAudioDevice, _id,
                         "  AudioDeviceBuffer::RequestPlayoutData failed!");
            return;
        }
        RTC_DCHECK_EQ(static_cast<size_t>(samples), play_frames_per_buffer_);

        // Copy decoded data into common byte buffer to ensure that it can be
        // written to the Java based audio track.
        samples = audio_device_buffer_->GetPlayoutData(playout_data_);
        RTC_DCHECK_EQ(static_cast<size_t>(samples), play_frames_per_buffer_);

    }

// ============================================================================
//                         Private functions.
// ============================================================================

    int32_t JavaAppAudioDevice::Available(bool& available, jmethodID methodID) {

        ScopedLocalRefFrame local_ref_frame(jni());
        jint result = jni()->CallIntMethod(*j_device_, methodID);
        CHECK_EXCEPTION(jni());

        if (result < 0) return -1;
        available = result == 1;
        return 0;

    }

    int32_t JavaAppAudioDevice::Delay(uint16_t& delayMS, jmethodID methodID) const {

        ScopedLocalRefFrame local_ref_frame(jni());
        jint result = jni()->CallIntMethod(*j_device_, methodID);
        CHECK_EXCEPTION(jni());

        if (result < 0) return -1;
        delayMS = (uint16_t) (result & 0xFFFF);
        return 0;

    }

    void JavaAppAudioDevice::SetupBuffer(const uint32_t samplesPerSec,
                                         int channels,
                                         size_t* dataSize,
                                         uint8_t** data,
                                         uint32_t* framesPerBuffer,
                                         jfieldID bufferField) {

        // Create a direct byte buffer and pass it to the Java audio device module.
        int bytesPerFrame = channels * (BITS_PER_SAMPLE / 8);
        *framesPerBuffer = samplesPerSec / BUFFERS_PER_SECOND;
        int bufferSize = bytesPerFrame * *framesPerBuffer;
        LOG(LS_INFO) << "JavaAppAudioDevice buffer size: " << bufferSize;

        if (*dataSize != bufferSize) {

            if (*data != NULL) {
                delete *data;
                dataSize = 0;
            }

            *data = new uint8_t[bufferSize];
            if (*data == NULL) {
                WEBRTC_TRACE(kTraceError, kTraceAudioDevice, _id,
                             "  Failed to allocate audio data buffer.");
                return;
            }
            *dataSize = (size_t) bufferSize;

            ScopedLocalRefFrame local_ref_frame(jni());
            jobject j_buffer = jni()->NewDirectByteBuffer(*data, bufferSize);
            jni()->SetObjectField(*j_device_, bufferField, j_buffer);
            CHECK_EXCEPTION(jni());

        }

    }

// ============================================================================
//                         JavaAppAudioDeviceModule.
// ============================================================================

    JavaAppAudioDeviceModule* JavaAppAudioDeviceModule::Create(
            const int32_t id, JavaAppAudioDevice *device) {

        // Create the generic ref counted (platform independent) implementation.
        RefCountImpl<JavaAppAudioDeviceModule>* audioDevice =
                new RefCountImpl<JavaAppAudioDeviceModule>(id, device);

        // Ensure that the generic audio buffer can communicate with the
        // platform-specific parts.
        if (audioDevice->AttachAudioBuffer() == -1)
        {
            delete audioDevice;
            return NULL;
        }

        WebRtcSpl_Init();

        return audioDevice;

    }

    JavaAppAudioDeviceModule::JavaAppAudioDeviceModule(const int32_t id, JavaAppAudioDevice* device) :
            AudioDeviceModuleImpl(id, AudioLayer::kPlatformDefaultAudio),
            device_(device) {

        SetAudioDevice(device);
        WEBRTC_TRACE(kTraceMemory, kTraceAudioDevice, id, "%s created", __FUNCTION__);

    }

// ============================================================================
//                              JNI exports.
// ============================================================================

    JOW(jlong, AppAudioDeviceModule_nativeWrapAppAudioDeviceModule)(
            JNIEnv* jni, jclass /*clazz*/, jobject j_module) {

        rtc::scoped_refptr<JavaAppAudioDeviceModule> module(
                JavaAppAudioDeviceModule::Create(0, new JavaAppAudioDevice(0, jni, j_module)));
        module->AddRef();
        int32_t count = module->Release();
        LOG(LS_INFO) << " -----> nativeWrapAppAudioDeviceModule ref count: " << count;
        return (jlong)module.release();

    }

    JOW(void, AppAudioDeviceModule_freeWrappedAppAudioDeviceModule)(
            JNIEnv* jni, jclass /*clazz*/, jlong j_p) {

        JavaAppAudioDeviceModule* module = reinterpret_cast<JavaAppAudioDeviceModule*>(j_p);

        module->AddRef();
        if (module->Release() > 1) {

            jni->ThrowNew(
                    jni->FindClass("java/lang/RuntimeException"),
                    "This AppAudioDeviceModule has active references to it and cannot be safely "
                    "deleted. This is most probably because a PeerConnectionFactory is holding "
                    "a reference to it. The factory must be disposed before disposing the ADM."
            );
            return;

        }

        delete module;

    }

    JOW(jint, AppAudioDeviceModule_nativeRecordingChannels)(
            JNIEnv* /*jni*/, jclass /*clazz*/, jlong nativeModule) {

        LOG(LS_INFO) << "nativeRecordingChannels called: " << nativeModule;
        AudioDeviceModule* module = reinterpret_cast<AudioDeviceModule*> (nativeModule);
        bool stereo;
        if (module->StereoRecording(&stereo) != 0) return -1;
        return stereo ? 2 : 1;

    }

    JOW(jint, AppAudioDeviceModule_nativePlayoutChannels)(
            JNIEnv* /*jni*/, jclass /*clazz*/, jlong nativeModule) {

        LOG(LS_INFO) << "nativeRecordingChannels called: " << nativeModule;
        AudioDeviceModule* module = reinterpret_cast<AudioDeviceModule*> (nativeModule);
        bool stereo;
        if (module->StereoPlayout(&stereo) != 0) return -1;
        return stereo ? 2 : 1;

    }

    JOW(void, AppAudioDeviceModule_nativeSetRecordingSampleRate)(
            JNIEnv* /*jni*/, jclass /*clazz*/, jlong nativeModule, jlong sampleRate) {

        LOG(LS_INFO) << "nativeSetRecordingSampleRate called: " << nativeModule << ", " << sampleRate;
        AudioDeviceModule* module = reinterpret_cast<AudioDeviceModule*> (nativeModule);
        module->SetRecordingSampleRate((uint32_t) sampleRate & 0xFFFFFFFF);

    }

    JOW(void, AppAudioDeviceModule_nativeSetPlayoutSampleRate)(
            JNIEnv* /*jni*/, jclass /*clazz*/, jlong nativeModule, jlong sampleRate) {

        LOG(LS_INFO) << "nativeSetPlayoutSampleRate called: " << nativeModule << ", " << sampleRate;
        AudioDeviceModule* module = reinterpret_cast<AudioDeviceModule*> (nativeModule);
        module->SetPlayoutSampleRate((uint32_t) sampleRate & 0xFFFFFFFF);

    }

    JOW(void, AppAudioDeviceModule_nativeDataIsRecorded)(
            JNIEnv* /*jni*/, jclass clazz, jlong nativeModule) {

        JavaAppAudioDeviceModule* module = reinterpret_cast<JavaAppAudioDeviceModule*> (nativeModule);
        module->device()->DataIsRecorded();

    }

    JOW(void, AppAudioDeviceModule_nativeGetPlayoutData)(
            JNIEnv* /*jni*/, jclass /*clazz*/, jlong nativeModule) {

        JavaAppAudioDeviceModule* module = reinterpret_cast<JavaAppAudioDeviceModule*> (nativeModule);
        module->device()->GetPlayoutData();

    }

}