package org.webrtc;

import java.nio.ByteBuffer;

public class AudioTrackSink {

  /** The real meat of the AudioTrackSinkInterface. */
  public static interface Callbacks {

    public void onData(ByteBuffer audioData,
                       int bitsPerSample,
                       int sampleRate,
                       int numberOfChannels,
                       int numberOfFrames);

  }

  long nativeAudioTrackSink;

  public AudioTrackSink(Callbacks callbacks) {
    nativeAudioTrackSink = nativeWrapAudioTrackSink(callbacks);
  }

  public void dispose() {
    if (nativeAudioTrackSink == 0) {
      // Already disposed.
      return;
    }
    freeWrappedAudioTrackSink(nativeAudioTrackSink);
    nativeAudioTrackSink = 0;
  }

  private static native long nativeWrapAudioTrackSink(Callbacks callbacks);

  private static native void freeWrappedAudioTrackSink(long nativeAudioTrackSink);

}