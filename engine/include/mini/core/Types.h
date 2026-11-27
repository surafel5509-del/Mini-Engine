#pragma once
#include <cstdint>
#include <string>

namespace mini {

// Strong-ish scalar aliases used across the engine.
using u8  = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using i32 = std::int32_t;
using f32 = float;
using f64 = double;

// ECS entity handle. 0 is reserved as "null".
using Entity = u32;
constexpr Entity kNullEntity = 0;

using AssetId = u32;
constexpr AssetId kNullAsset = 0;

} // namespace mini
