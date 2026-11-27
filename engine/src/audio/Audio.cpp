#include "mini/audio/Audio.h"
#include "mini/core/Log.h"

namespace mini {

bool AudioEngine::init() {
    mInitialized = true;
    logInfo("AudioEngine initialized (silent backend, Phase 1)");
    return true;
}

void AudioEngine::shutdown() { mInitialized = false; }

void AudioEngine::playTone(float frequencyHz, float durationSec, float volume) {
    if (!mInitialized) return;
    // Phase 4: route to Oboe/OpenSL ES. Phase 1 is intentionally silent.
    (void)frequencyHz; (void)durationSec; (void)volume;
}

} // namespace mini
