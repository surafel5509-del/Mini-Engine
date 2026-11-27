#pragma once
#include <unordered_map>
#include <unordered_set>
#include "mini/core/Types.h"

namespace mini {

// Input state snapshot fed by the platform layer (touch, keyboard,
// gamepad). The editor and games poll this each frame.
class Input {
public:
    void setKey(int keyCode, bool down);
    bool isKeyDown(int keyCode) const;
    void setTouch(u32 pointerId, float x, float y, bool down);
    bool isTouchDown(u32 pointerId) const;
    void clearFrame() { mPressedThisFrame.clear(); }

private:
    std::unordered_set<int> mKeysDown;
    std::unordered_set<int> mPressedThisFrame;
    struct Touch { float x = 0, y = 0; bool down = false; };
    std::unordered_map<u32, Touch> mTouches;
};

} // namespace mini
