package org.webrtc;

import java.nio.ByteBuffer;
import java.util.ConcurrentModificationException;
import java.util.concurrent.locks.Lock;
import java.util.concurrent.locks.ReentrantLock;

public abstract class AppAudioDeviceModule {

    public static final int MAX_DEVICE_INDEX = 65535;

    long nativeModule;

    private boolean initialized = false;

    private boolean playInitialized;

    private boolean playing;

    private boolean recInitialized;

    private boolean recording;

    private ByteBuffer recordingBuffer;

    private ByteBuffer playoutBuffer;

    private final Lock recordingLock = new ReentrantLock();

    private final Lock playoutLock = new ReentrantLock();

    public AppAudioDeviceModule() { this.nativeModule = nativeWrapAppAudioDeviceModule(this); }

    public void dispose() {

        if (nativeModule == 0) return; // Already disposed.
        freeWrappedAppAudioDeviceModule(nativeModule);
        nativeModule = 0;
        recordingBuffer = null;
        playoutBuffer = null;

    }

    // ============================================================================
    //                       Main initialization and termination.
    // ============================================================================

    protected synchronized int init() {

        onInit();
        initialized = true;
        return 0;

    }

    protected synchronized int terminate() {

        if (!initialized) return 0;
        onTerminate();
        initialized = false;
        return 0;

    }

    protected synchronized boolean initialized() { return initialized; }

    protected abstract void onInit();
    protected abstract void onTerminate();

    // ============================================================================
    //                       Audio transport initialization.
    // ============================================================================

    protected abstract int playoutIsAvailable();
    protected abstract int recordingIsAvailable();

    protected synchronized int initPlayout() {

        // Sanity check. Channels must be initialized according to availability up to
        // this point.
        int expectedChannels = stereoPlayoutIsAvailable() ? 2 : 1;
        int channels = playoutChannels();
        if (channels != expectedChannels)
            throw new RuntimeException("Unexpected playout channels: " + channels);

        if (playInitialized || !onInitPlayout()) return -1;
        setPlayoutSampleRate(playoutSampleRate());
        playInitialized = true;
        return 0;

    }

    protected synchronized boolean playoutIsInitialized() { return playInitialized; }

    protected synchronized int initRecording() {

        // Sanity check. Channels must be initialized according to availability up to
        // this point.
        int expectedChannels = stereoRecordingIsAvailable() ? 2 : 1;
        int channels = recordingChannels();
        if (channels != expectedChannels)
            throw new RuntimeException("Unexpected recording channels: " + channels);

        if (recInitialized || !onInitRecording()) return -1;
        setRecordingSampleRate(recordingSampleRate());
        recInitialized = true;
        return 0;

    }

    protected synchronized boolean recordingIsInitialized() { return recInitialized; }

    protected abstract boolean onInitPlayout();
    protected abstract long playoutSampleRate();
    protected abstract boolean onInitRecording();
    protected abstract long recordingSampleRate();

    // ============================================================================
    //                           Audio transport control.
    // ============================================================================

    protected synchronized int startPlayout() {

        if (!playInitialized) return -1;
        if (playing) return 0;

        onStartPlayout();
        playing = true;
        return 0;

    }

    protected synchronized int stopPlayout() {

        if (!playInitialized) return 0;

        onStopPlayout();
        playInitialized = false;
        playing = false;
        return 0;

    }

    protected synchronized boolean playing() { return playing; }

    protected synchronized int startRecording() {

        if (!recInitialized) return -1;
        if (recording) return 0;

        onStartRecording();
        recording = true;
        return 0;

    }

    protected synchronized int stopRecording() {

        if (!recInitialized) return 0;

        onStopRecording();
        recInitialized = false;
        recording = false;
        return 0;

    }

    protected synchronized boolean recording() { return recording; }

    protected abstract void onStartPlayout();
    protected abstract void onStopPlayout();
    protected abstract void onStartRecording();
    protected abstract void onStopRecording();

    // ============================================================================
    //                        Delay information and control.
    // ============================================================================

    protected abstract int playoutDelay();
    protected abstract int recordingDelay();

    // ============================================================================
    //                              Stereo support.
    // ============================================================================

    protected abstract boolean stereoPlayoutIsAvailable();
    protected abstract boolean stereoRecordingIsAvailable();

    protected int playoutChannels() {

        int channels = nativePlayoutChannels(nativeModule);
        if (channels < 1 || channels > 2)
            throw new RuntimeException("Unexpected playout channels: " + channels);
        return channels;

    }

    protected int recordingChannels() {

        int channels = nativeRecordingChannels(nativeModule);
        if (channels < 1 || channels > 2)
            throw new RuntimeException("Unexpected recording channels: " + channels);
        return channels;

    }

    // ============================================================================
    //                    Native sample rate controls (samples/sec).
    // ============================================================================

    // These methods setup the respective parameters of the native audio device
    // buffer. Direct byte buffers are allocated in native code as a result and
    // references to them are passed to this object by setting the
    // 'recordingBuffer' and 'playoutBuffer' fields. This object can then
    // exchange data with the native side using the attached buffers and the
    // dataIsRecorded() and getPlayoutData() methods.

    private void setRecordingSampleRate(long sampleRate) {

        recordingLock.lock();
        try {

            recordingBuffer = null;
            nativeSetRecordingSampleRate(nativeModule, sampleRate);
            assert recordingBuffer != null;

        } finally { recordingLock.unlock(); }

    }
    private void setPlayoutSampleRate(long sampleRate) {

        playoutLock.lock();
        try {

            playoutBuffer = null;
            nativeSetPlayoutSampleRate(nativeModule, sampleRate);
            assert playoutBuffer != null;
            playoutBuffer.flip();

        } finally { playoutLock.unlock(); }

    }

    // ============================================================================
    //                          Playout & recording status.
    // ============================================================================

    protected abstract boolean playoutWarning();
    protected abstract boolean playoutError();
    protected abstract boolean recordingWarning();
    protected abstract boolean recordingError();
    protected abstract void clearPlayoutWarning();
    protected abstract void clearPlayoutError();
    protected abstract void clearRecordingWarning();
    protected abstract void clearRecordingError();

    // ============================================================================
    //                       Audio Device Buffer.
    // ============================================================================

    protected void dataIsRecorded(byte[] src) { dataIsRecorded(src, 0, src.length); }

    protected void dataIsRecorded(byte[] src, int offset, int length) {

        // Check parameters.
        if (src.length - offset > length)
            throw new IllegalArgumentException("Invalid offset-length parameters.");

        // Prevent concurrent access to the recording buffer.
        boolean gotLock = false;
        try {

            if (gotLock = recordingLock.tryLock()) unsafeDataIsRecorded(src, offset, length);
            else throw new ConcurrentModificationException("Concurrent access to recording buffer.");

        } finally { if (gotLock) recordingLock.unlock(); }

    }

    protected void dataIsRecorded(ByteBuffer src) {

        // Prevent concurrent access to the recording buffer.
        boolean gotLock = false;
        try {

            if (gotLock = recordingLock.tryLock()) unsafeDataIsRecorded(src);
            else throw new ConcurrentModificationException("Concurrent access to recording buffer.");

        } finally { if (gotLock) recordingLock.unlock(); }

    }

    private void unsafeDataIsRecorded(byte[] src, int offset, int length) {

        int remaining;

        while (length > 0) {

            remaining = recordingBuffer.remaining();
            if (remaining >= length) {

                recordingBuffer.put(src, offset, length);
                remaining -= length;
                length = 0;

            } else {

                recordingBuffer.put(src, offset, remaining);
                offset += remaining;
                length -= remaining;
                remaining = 0;

            }

            if (remaining == 0) {

                nativeDataIsRecorded(nativeModule);
                recordingBuffer.clear();

            }

        }

    }

    private void unsafeDataIsRecorded(ByteBuffer src) {

        int srcRemaining = src.remaining();
        int srcLimit = src.limit();
        int recRemaining;

        while (srcRemaining > 0) {

            recRemaining = recordingBuffer.remaining();
            if (recRemaining >= srcRemaining) {

                recordingBuffer.put(src);
                recRemaining -= srcRemaining;
                srcRemaining = 0;

            } else {

                src.limit(src.position() + recRemaining);
                recordingBuffer.put(src);
                src.limit(srcLimit);
                srcRemaining -= recRemaining;
                recRemaining = 0;

            }

            if (recRemaining == 0) {

                nativeDataIsRecorded(nativeModule);
                recordingBuffer.clear();

            }

        }

    }

    protected void getPlayoutData(byte[] dst) { getPlayoutData(dst, 0, dst.length); }

    protected void getPlayoutData(byte[] dst, int offset, int length) {

        if (dst.length - offset > length)
            throw new IllegalArgumentException("Invalid offset-length parameters.");

        if (length == 0) return;

        // Prevent concurrent access to the playback buffer.
        boolean gotLock = false;
        try {

            if (gotLock = playoutLock.tryLock()) unsafeGetPlayoutData(dst, offset, length);
            else throw new ConcurrentModificationException("Concurrent access to recording buffer.");

        } finally { if (gotLock) playoutLock.unlock(); }

    }

    protected void getPlayoutData(ByteBuffer dst) {

        // Prevent concurrent access to the playback buffer.
        boolean gotLock = false;
        try {

            if (gotLock = playoutLock.tryLock()) unsafeGetPlayoutData(dst);
            else throw new ConcurrentModificationException("Concurrent access to recording buffer.");

        } finally { if (gotLock) playoutLock.unlock(); }

    }

    private void unsafeGetPlayoutData(byte[] dst, int offset, int length) {

        int remaining;

        do {

            remaining = playoutBuffer.remaining();
            if (remaining == 0) {

                nativeGetPlayoutData(nativeModule);
                remaining = playoutBuffer.clear().remaining();

            }

            if (remaining >= length) {

                playoutBuffer.get(dst, offset, length);
                length = 0;

            } else {

                playoutBuffer.get(dst, offset, remaining);
                offset += remaining;
                length -= remaining;

            }


        } while (length > 0);

    }

    private void unsafeGetPlayoutData(ByteBuffer dst) {

        int playRemaining;
        int dstRemaining;

        do {

            playRemaining = playoutBuffer.remaining();
            dstRemaining = dst.remaining();

            if (playRemaining == 0) {

                nativeGetPlayoutData(nativeModule);
                playRemaining = playoutBuffer.clear().remaining();

            }

            if (playRemaining >= dstRemaining) {

                playoutBuffer.limit(playoutBuffer.position() + dstRemaining);
                dst.put(playoutBuffer);
                playoutBuffer.limit(playoutBuffer.capacity());
                dstRemaining = 0;

            } else {

                dst.put(playoutBuffer);
                dstRemaining -= playRemaining;

            }

        } while (dstRemaining > 0);

    }

    // ============================================================================
    //                          Native helpers.
    // ============================================================================

    private static native long nativeWrapAppAudioDeviceModule(AppAudioDeviceModule module);
    private static native void freeWrappedAppAudioDeviceModule(long nativeModule);

    private static native int nativeRecordingChannels(long nativeModule);
    private static native int nativePlayoutChannels(long nativeModule);

    private static native void nativeSetRecordingSampleRate(long nativeModule, long sampleRate);
    private static native void nativeSetPlayoutSampleRate(long nativeModule, long sampleRate);

    private native void nativeDataIsRecorded(long nativeModule);
    private native void nativeGetPlayoutData(long nativeModule);

}