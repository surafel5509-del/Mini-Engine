#include "mini/input/Input.h"

namespace mini {

void Input::setKey(int keyCode, bool down) {
    if (down) {
        if (mKeysDown.insert(keyCode).second) mPressedThisFrame.insert(keyCode);
    } else {
        mKeysDown.erase(keyCode);
    }
}

bool Input::isKeyDown(int keyCode) const {
    return mKeysDown.count(keyCode) != 0;
}

void Input::setTouch(u32 pointerId, float x, float y, bool down) {
    mTouches[pointerId] = Touch{x, y, down};
}

bool Input::isTouchDown(u32 pointerId) const {
    auto it = mTouches.find(pointerId);
    return it != mTouches.end() && it->second.down;
}

} // namespace mini
