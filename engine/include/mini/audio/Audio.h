#pragma once
#include "mini/core/Types.h"

namespace mini {

// Audio facade. Phase 1: interface + silent backend. Phase 4 wires this
// to Oboe/OpenSL ES on Android.
class AudioEngine {
public:
    bool init();
    void shutdown();
    void playTone(float frequencyHz, float durationSec, float volume);
    void setMasterVolume(float v) { mVolume = v; }

private:
    bool mInitialized = false;
    float mVolume = 1.0f;
};

} // namespace mini
