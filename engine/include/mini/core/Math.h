#pragma once
#include <cmath>
#include "mini/core/Types.h"

namespace mini {

constexpr f32 kPi = 3.14159265358979323846f;

inline f32 degToRad(f32 d) { return d * (kPi / 180.0f); }
inline f32 radToDeg(f32 r) { return r * (180.0f / kPi); }
inline f32 clamp(f32 v, f32 lo, f32 hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
inline f32 lerp(f32 a, f32 b, f32 t) { return a + (b - a) * t; }

struct Vec3 {
    f32 x = 0, y = 0, z = 0;
    Vec3() = default;
    Vec3(f32 x_, f32 y_, f32 z_) : x(x_), y(y_), z(z_) {}
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(f32 s) const { return {x * s, y * s, z * s}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    f32 length() const { return std::sqrt(x*x + y*y + z*z); }
    Vec3 normalized() const {
        f32 l = length();
        return l > 1e-6f ? (*this) * (1.0f / l) : Vec3{0, 1, 0};
    }
};

inline f32 dot(const Vec3& a, const Vec3& b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x};
}

struct Vec4 {
    f32 x = 0, y = 0, z = 0, w = 1;
    Vec4() = default;
    Vec4(f32 x_, f32 y_, f32 z_, f32 w_) : x(x_), y(y_), z(z_), w(w_) {}
};

// Column-major 4x4 matrix (OpenGL / Vulkan convention).
struct Mat4 {
    f32 m[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};

    static Mat4 identity() { return Mat4{}; }

    static Mat4 translation(const Vec3& t) {
        Mat4 r;
        r.m[0]=1; r.m[1]=0; r.m[2]=0; r.m[3]=0;
        r.m[4]=0; r.m[5]=1; r.m[6]=0; r.m[7]=0;
        r.m[8]=0; r.m[9]=0; r.m[10]=1; r.m[11]=0;
        r.m[12]=t.x; r.m[13]=t.y; r.m[14]=t.z; r.m[15]=1;
        return r;
    }

    static Mat4 scale(const Vec3& s) {
        Mat4 r;
        r.m[0]=s.x; r.m[5]=s.y; r.m[10]=s.z;
        return r;
    }

    static Mat4 perspective(f32 fovYRad, f32 aspect, f32 near, f32 far) {
        Mat4 r;
        f32 f = 1.0f / std::tan(fovYRad * 0.5f);
        r.m[0]=f/aspect; r.m[5]=f;
        r.m[10]=(far+near)/(near-far); r.m[11]=-1;
        r.m[14]=(2*far*near)/(near-far); r.m[15]=0;
        return r;
    }

    static Mat4 orthographic(f32 left, f32 right, f32 bottom, f32 top,
                             f32 nearPlane, f32 farPlane) {
        Mat4 r;
        r.m[0] = 2.0f / (right - left);
        r.m[5] = 2.0f / (top - bottom);
        r.m[10] = -2.0f / (farPlane - nearPlane);
        r.m[12] = -(right + left) / (right - left);
        r.m[13] = -(top + bottom) / (top - bottom);
        r.m[14] = -(farPlane + nearPlane) / (farPlane - nearPlane);
        r.m[15] = 1.0f;
        return r;
    }

    static Mat4 lookAt(const Vec3& eye, const Vec3& target, const Vec3& up) {
        Vec3 f = (target - eye).normalized();
        Vec3 s = cross(f, up).normalized();
        Vec3 u = cross(s, f);
        Mat4 r;
        r.m[0]=s.x;   r.m[1]=u.x;   r.m[2]=-f.x;  r.m[3]=0;
        r.m[4]=s.y;   r.m[5]=u.y;   r.m[6]=-f.y;  r.m[7]=0;
        r.m[8]=s.z;   r.m[9]=u.z;   r.m[10]=-f.z; r.m[11]=0;
        r.m[12]=-dot(s, eye); r.m[13]=-dot(u, eye); r.m[14]=dot(f, eye); r.m[15]=1;
        return r;
    }

    Mat4 operator*(const Mat4& o) const {
        Mat4 r;
        for (int c = 0; c < 4; ++c)
            for (int row = 0; row < 4; ++row) {
                f32 sum = 0;
                for (int k = 0; k < 4; ++k) sum += m[k*4+row] * o.m[c*4+k];
                r.m[c*4+row] = sum;
            }
        return r;
    }
};

struct Quaternion {
    f32 x = 0, y = 0, z = 0, w = 1;
    static Quaternion identity() { return {}; }
    static Quaternion fromEulerDeg(const Vec3& e) {
        f32 cx = std::cos(degToRad(e.x) * 0.5f), sx = std::sin(degToRad(e.x) * 0.5f);
        f32 cy = std::cos(degToRad(e.y) * 0.5f), sy = std::sin(degToRad(e.y) * 0.5f);
        f32 cz = std::cos(degToRad(e.z) * 0.5f), sz = std::sin(degToRad(e.z) * 0.5f);
        Quaternion q;
        q.w = cx*cy*cz + sx*sy*sz;
        q.x = sx*cy*cz - cx*sy*sz;
        q.y = cx*sy*cz + sx*cy*sz;
        q.z = cx*cy*sz - sx*sy*cz;
        return q;
    }
    Mat4 toMat4() const {
        Mat4 r;
        f32 xx=x*x, yy=y*y, zz=z*z, xy=x*y, xz=x*z, yz=y*z, wx=w*x, wy=w*y, wz=w*z;
        r.m[0]=1-2*(yy+zz); r.m[1]=2*(xy+wz);   r.m[2]=2*(xz-wy);
        r.m[4]=2*(xy-wz);   r.m[5]=1-2*(xx+zz); r.m[6]=2*(yz+wx);
        r.m[8]=2*(xz+wy);   r.m[9]=2*(yz-wx);   r.m[10]=1-2*(xx+yy);
        return r;
    }
};

} // namespace mini
