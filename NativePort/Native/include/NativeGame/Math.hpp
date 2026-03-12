#pragma once

#include <algorithm>
#include <cmath>

namespace NativeGame {

constexpr float kPi = 3.14159265358979323846f;

inline float radians(float degrees)
{
    return degrees * (kPi / 180.0f);
}

inline float degrees(float radiansValue)
{
    return radiansValue * (180.0f / kPi);
}

inline float clamp(float value, float minValue, float maxValue)
{
    return std::clamp(value, minValue, maxValue);
}

inline float mix(float a, float b, float t)
{
    return a + ((b - a) * t);
}

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Vec4 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;
};

inline Vec3 operator+(const Vec3& a, const Vec3& b)
{
    return { a.x + b.x, a.y + b.y, a.z + b.z };
}

inline Vec3 operator-(const Vec3& a, const Vec3& b)
{
    return { a.x - b.x, a.y - b.y, a.z - b.z };
}

inline Vec3 operator-(const Vec3& v)
{
    return { -v.x, -v.y, -v.z };
}

inline Vec3 operator*(const Vec3& v, float scalar)
{
    return { v.x * scalar, v.y * scalar, v.z * scalar };
}

inline Vec3 operator*(float scalar, const Vec3& v)
{
    return v * scalar;
}

inline Vec3 operator/(const Vec3& v, float scalar)
{
    return { v.x / scalar, v.y / scalar, v.z / scalar };
}

inline Vec3& operator+=(Vec3& a, const Vec3& b)
{
    a.x += b.x;
    a.y += b.y;
    a.z += b.z;
    return a;
}

inline Vec3& operator-=(Vec3& a, const Vec3& b)
{
    a.x -= b.x;
    a.y -= b.y;
    a.z -= b.z;
    return a;
}

inline Vec3& operator*=(Vec3& a, float scalar)
{
    a.x *= scalar;
    a.y *= scalar;
    a.z *= scalar;
    return a;
}

inline Vec3 hadamard(const Vec3& a, const Vec3& b)
{
    return { a.x * b.x, a.y * b.y, a.z * b.z };
}

inline Vec4 operator*(const Vec4& v, float scalar)
{
    return { v.x * scalar, v.y * scalar, v.z * scalar, v.w * scalar };
}

inline Vec4 operator+(const Vec4& a, const Vec4& b)
{
    return { a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w };
}

inline float dot(const Vec3& a, const Vec3& b)
{
    return (a.x * b.x) + (a.y * b.y) + (a.z * b.z);
}

inline Vec3 cross(const Vec3& a, const Vec3& b)
{
    return {
        (a.y * b.z) - (a.z * b.y),
        (a.z * b.x) - (a.x * b.z),
        (a.x * b.y) - (a.y * b.x)
    };
}

inline float lengthSquared(const Vec3& v)
{
    return dot(v, v);
}

inline float length(const Vec3& v)
{
    return std::sqrt(lengthSquared(v));
}

inline bool isFinite(float value)
{
    return std::isfinite(value) != 0;
}

inline float sanitize(float value, float fallback)
{
    return isFinite(value) ? value : fallback;
}

inline Vec3 normalize(const Vec3& v, const Vec3& fallback = { 0.0f, 0.0f, 0.0f })
{
    const float len = length(v);
    if (len <= 1.0e-8f) {
        return fallback;
    }
    return v / len;
}

inline Vec3 clampMagnitude(const Vec3& v, float maxMagnitude)
{
    const float len = length(v);
    if (len <= maxMagnitude || len <= 1.0e-8f) {
        return v;
    }
    return v * (maxMagnitude / len);
}

inline Vec3 lerp(const Vec3& a, const Vec3& b, float t)
{
    return {
        mix(a.x, b.x, t),
        mix(a.y, b.y, t),
        mix(a.z, b.z, t)
    };
}

struct Quat {
    float w = 1.0f;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

inline Quat quatIdentity()
{
    return {};
}

inline Quat quatMultiply(const Quat& a, const Quat& b)
{
    return {
        (a.w * b.w) - (a.x * b.x) - (a.y * b.y) - (a.z * b.z),
        (a.w * b.x) + (a.x * b.w) + (a.y * b.z) - (a.z * b.y),
        (a.w * b.y) - (a.x * b.z) + (a.y * b.w) + (a.z * b.x),
        (a.w * b.z) + (a.x * b.y) - (a.y * b.x) + (a.z * b.w)
    };
}

inline Quat quatConjugate(const Quat& q)
{
    return { q.w, -q.x, -q.y, -q.z };
}

inline Quat quatNormalize(const Quat& q)
{
    const float len = std::sqrt((q.w * q.w) + (q.x * q.x) + (q.y * q.y) + (q.z * q.z));
    if (len <= 1.0e-8f) {
        return quatIdentity();
    }
    return {
        q.w / len,
        q.x / len,
        q.y / len,
        q.z / len
    };
}

inline Quat quatFromAxisAngle(const Vec3& axis, float angle)
{
    const Vec3 n = normalize(axis, { 1.0f, 0.0f, 0.0f });
    const float half = angle * 0.5f;
    const float s = std::sin(half);
    return {
        std::cos(half),
        n.x * s,
        n.y * s,
        n.z * s
    };
}

inline Vec3 rotateVector(const Quat& quat, const Vec3& v)
{
    const Quat qv { 0.0f, v.x, v.y, v.z };
    const Quat result = quatMultiply(quatMultiply(quat, qv), quatConjugate(quat));
    return { result.x, result.y, result.z };
}

inline Vec3 forwardFromRotation(const Quat& q)
{
    return normalize(rotateVector(q, { 0.0f, 0.0f, 1.0f }), { 0.0f, 0.0f, 1.0f });
}

inline Vec3 upFromRotation(const Quat& q)
{
    return normalize(rotateVector(q, { 0.0f, 1.0f, 0.0f }), { 0.0f, 1.0f, 0.0f });
}

inline Vec3 rightFromRotation(const Quat& q)
{
    return normalize(rotateVector(q, { 1.0f, 0.0f, 0.0f }), { 1.0f, 0.0f, 0.0f });
}

}  // namespace NativeGame
