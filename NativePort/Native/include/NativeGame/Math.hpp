#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>

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
    constexpr Vec2() = default;
    constexpr Vec2(float xValue, float yValue)
        : x(xValue)
        , y(yValue)
    {
    }

    float x = 0.0f;
    float y = 0.0f;
};

struct Vec3 {
    constexpr Vec3() = default;
    constexpr Vec3(float xValue, float yValue, float zValue)
        : x(xValue)
        , y(yValue)
        , z(zValue)
    {
    }

    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Vec4 {
    constexpr Vec4() = default;
    constexpr Vec4(float xValue, float yValue, float zValue, float wValue)
        : x(xValue)
        , y(yValue)
        , z(zValue)
        , w(wValue)
    {
    }

    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;
};

struct DVec3 {
    constexpr DVec3() = default;
    constexpr DVec3(double xValue, double yValue, double zValue)
        : x(xValue)
        , y(yValue)
        , z(zValue)
    {
    }

    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

enum class WorldShape : std::uint8_t {
    Plane = 0,
    Planet = 1
};

struct GeodeticCoord {
    double latitudeDeg = 0.0;
    double longitudeDeg = 0.0;
    double altitudeMeters = 0.0;
};

struct PlanetTileId {
    int face = 0;
    int lod = 0;
    int tx = 0;
    int ty = 0;
};

struct PlanetConfig {
    double radiusMeters = 6371000.0;
    double gravitationalParameter = 3.986004418e14;
    double rotationRateRadPerSec = 7.2921159e-5;
    double atmosphereHeightMeters = 120000.0;
    GeodeticCoord localOrigin {};
};

struct PlanetLocalFrame {
    DVec3 surfaceOriginInertial {};
    DVec3 east { 1.0, 0.0, 0.0 };
    DVec3 up { 0.0, 1.0, 0.0 };
    DVec3 north { 0.0, 0.0, 1.0 };
    DVec3 spinAxis { 0.0, 1.0, 0.0 };
    double originRadiusMeters = 6371000.0;
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

inline DVec3 operator+(const DVec3& a, const DVec3& b)
{
    return { a.x + b.x, a.y + b.y, a.z + b.z };
}

inline DVec3 operator-(const DVec3& a, const DVec3& b)
{
    return { a.x - b.x, a.y - b.y, a.z - b.z };
}

inline DVec3 operator-(const DVec3& v)
{
    return { -v.x, -v.y, -v.z };
}

inline DVec3 operator*(const DVec3& v, double scalar)
{
    return { v.x * scalar, v.y * scalar, v.z * scalar };
}

inline DVec3 operator*(double scalar, const DVec3& v)
{
    return v * scalar;
}

inline DVec3 operator/(const DVec3& v, double scalar)
{
    return { v.x / scalar, v.y / scalar, v.z / scalar };
}

inline DVec3& operator+=(DVec3& a, const DVec3& b)
{
    a.x += b.x;
    a.y += b.y;
    a.z += b.z;
    return a;
}

inline DVec3& operator-=(DVec3& a, const DVec3& b)
{
    a.x -= b.x;
    a.y -= b.y;
    a.z -= b.z;
    return a;
}

inline DVec3& operator*=(DVec3& a, double scalar)
{
    a.x *= scalar;
    a.y *= scalar;
    a.z *= scalar;
    return a;
}

inline Vec3 vec3FromList(std::initializer_list<float> values)
{
    Vec3 result {};
    auto it = values.begin();
    if (it != values.end()) {
        result.x = *it++;
    }
    if (it != values.end()) {
        result.y = *it++;
    }
    if (it != values.end()) {
        result.z = *it++;
    }
    return result;
}

inline DVec3 dvec3FromList(std::initializer_list<double> values)
{
    DVec3 result {};
    auto it = values.begin();
    if (it != values.end()) {
        result.x = *it++;
    }
    if (it != values.end()) {
        result.y = *it++;
    }
    if (it != values.end()) {
        result.z = *it++;
    }
    return result;
}

inline float dot(const Vec3& a, const Vec3& b)
{
    return (a.x * b.x) + (a.y * b.y) + (a.z * b.z);
}

inline double dot(const DVec3& a, const DVec3& b)
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

inline DVec3 cross(const DVec3& a, const DVec3& b)
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

inline float length(std::initializer_list<float> values)
{
    return length(vec3FromList(values));
}

inline double lengthSquared(const DVec3& v)
{
    return dot(v, v);
}

inline double length(const DVec3& v)
{
    return std::sqrt(lengthSquared(v));
}

inline double length(std::initializer_list<double> values)
{
    return length(dvec3FromList(values));
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

inline Vec3 normalize(std::initializer_list<float> values, std::initializer_list<float> fallback)
{
    return normalize(vec3FromList(values), vec3FromList(fallback));
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

inline DVec3 lerp(const DVec3& a, const DVec3& b, double t)
{
    return {
        a.x + ((b.x - a.x) * t),
        a.y + ((b.y - a.y) * t),
        a.z + ((b.z - a.z) * t)
    };
}

inline Vec3 toVec3(const DVec3& value)
{
    return {
        static_cast<float>(value.x),
        static_cast<float>(value.y),
        static_cast<float>(value.z)
    };
}

inline DVec3 toDVec3(const Vec3& value)
{
    return {
        static_cast<double>(value.x),
        static_cast<double>(value.y),
        static_cast<double>(value.z)
    };
}

inline DVec3 normalize(const DVec3& v, const DVec3& fallback = { 0.0, 0.0, 0.0 })
{
    const double len = length(v);
    if (len <= 1.0e-12) {
        return fallback;
    }
    return v / len;
}

inline DVec3 normalize(std::initializer_list<double> values, std::initializer_list<double> fallback)
{
    return normalize(dvec3FromList(values), dvec3FromList(fallback));
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

inline Quat QuatFromBasis(const Vec3& right, const Vec3& up, const Vec3& forward)
{
    const float m00 = right.x;
    const float m01 = up.x;
    const float m02 = forward.x;

    const float m10 = right.y;
    const float m11 = up.y;
    const float m12 = forward.y;

    const float m20 = right.z;
    const float m21 = up.z;
    const float m22 = forward.z;

    Quat q {};

    const float trace = m00 + m11 + m22;
    if (trace > 0.0f) {
        const float s = std::sqrt(trace + 1.0f) * 2.0f;
        q.w = 0.25f * s;
        q.x = (m21 - m12) / s;
        q.y = (m02 - m20) / s;
        q.z = (m10 - m01) / s;
    } else if (m00 > m11 && m00 > m22) {
        const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
        q.w = (m21 - m12) / s;
        q.x = 0.25f * s;
        q.y = (m01 + m10) / s;
        q.z = (m02 + m20) / s;
    } else if (m11 > m22) {
        const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
        q.w = (m02 - m20) / s;
        q.x = (m01 + m10) / s;
        q.y = 0.25f * s;
        q.z = (m12 + m21) / s;
    } else {
        const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
        q.w = (m10 - m01) / s;
        q.x = (m02 + m20) / s;
        q.y = (m12 + m21) / s;
        q.z = 0.25f * s;
    }

    return q;
}

inline Quat quatNormalizeSafe(const Quat& q)
{
    const float lenSq = (q.w * q.w) + (q.x * q.x) + (q.y * q.y) + (q.z * q.z);
    if (lenSq <= 1.0e-8f) {
        return quatIdentity();
    }

    const float invLen = 1.0f / std::sqrt(lenSq);
    return {
        q.w * invLen,
        q.x * invLen,
        q.y * invLen,
        q.z * invLen
    };
}

inline Quat quatFromBasisOrthonormal(const Vec3& inputRight, const Vec3& inputUp, const Vec3& inputForward)
{
    Vec3 forward = normalize(inputForward, { 0.0f, 0.0f, 1.0f });
    Vec3 up = normalize(inputUp, { 0.0f, 1.0f, 0.0f });
    const Vec3 rightHint = normalize(inputRight, { 1.0f, 0.0f, 0.0f });

    Vec3 right = cross(up, forward);
    if (lengthSquared(right) <= 1.0e-8f) {
        right = rightHint - (forward * dot(rightHint, forward));
    }
    right = normalize(right, { 1.0f, 0.0f, 0.0f });

    up = cross(forward, right);
    if (lengthSquared(up) <= 1.0e-8f) {
        up = inputUp - (forward * dot(inputUp, forward));
    }
    up = normalize(up, { 0.0f, 1.0f, 0.0f });

    return quatNormalizeSafe(QuatFromBasis(right, up, forward));
}

inline Quat nlerp(const Quat& a, const Quat& b, float t)
{
    Quat end = b;
    const float d = (a.w * b.w) + (a.x * b.x) + (a.y * b.y) + (a.z * b.z);
    if (d < 0.0f) {
        end.w = -end.w;
        end.x = -end.x;
        end.y = -end.y;
        end.z = -end.z;
    }

    return quatNormalizeSafe({
        mix(a.w, end.w, t),
        mix(a.x, end.x, t),
        mix(a.y, end.y, t),
        mix(a.z, end.z, t)
    });
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

inline DVec3 rotateAroundAxis(const DVec3& value, const DVec3& axis, double radiansValue)
{
    const DVec3 unitAxis = normalize(axis, { 0.0, 1.0, 0.0 });
    const double c = std::cos(radiansValue);
    const double s = std::sin(radiansValue);
    return (value * c) + (cross(unitAxis, value) * s) + (unitAxis * (dot(unitAxis, value) * (1.0 - c)));
}

inline DVec3 rotateAroundPlanetSpin(const DVec3& value, double radiansValue)
{
    return rotateAroundAxis(value, { 0.0, 1.0, 0.0 }, radiansValue);
}

inline double wrapAngleRadians(double angle)
{
    const double twoPi = static_cast<double>(kPi) * 2.0;
    angle = std::fmod(angle, twoPi);
    if (angle > static_cast<double>(kPi)) {
        angle -= twoPi;
    } else if (angle < -static_cast<double>(kPi)) {
        angle += twoPi;
    }
    return angle;
}

inline double wrapLongitudeDegrees(double longitudeDeg)
{
    double wrapped = std::fmod(longitudeDeg, 360.0);
    if (wrapped > 180.0) {
        wrapped -= 360.0;
    } else if (wrapped < -180.0) {
        wrapped += 360.0;
    }
    return wrapped;
}

inline GeodeticCoord normalizeGeodetic(GeodeticCoord coord)
{
    coord.latitudeDeg = std::clamp(coord.latitudeDeg, -90.0, 90.0);
    coord.longitudeDeg = wrapLongitudeDegrees(coord.longitudeDeg);
    return coord;
}

inline DVec3 planetFixedFromGeodetic(const GeodeticCoord& coordInput, const PlanetConfig& config)
{
    const GeodeticCoord coord = normalizeGeodetic(coordInput);
    const double latRad = coord.latitudeDeg * (static_cast<double>(kPi) / 180.0);
    const double lonRad = coord.longitudeDeg * (static_cast<double>(kPi) / 180.0);
    const double cosLat = std::cos(latRad);
    const double sinLat = std::sin(latRad);
    const double cosLon = std::cos(lonRad);
    const double sinLon = std::sin(lonRad);
    const double radius = config.radiusMeters + coord.altitudeMeters;
    return {
        radius * cosLat * cosLon,
        radius * sinLat,
        radius * cosLat * sinLon
    };
}

inline GeodeticCoord geodeticFromPlanetFixed(const DVec3& fixedPosition, const PlanetConfig& config)
{
    const double radius = std::max(1.0, length(fixedPosition));
    const double latRad = std::asin(std::clamp(fixedPosition.y / radius, -1.0, 1.0));
    const double lonRad = std::atan2(fixedPosition.z, fixedPosition.x);
    return normalizeGeodetic({
        latRad * (180.0 / static_cast<double>(kPi)),
        lonRad * (180.0 / static_cast<double>(kPi)),
        radius - config.radiusMeters
    });
}

inline PlanetLocalFrame makePlanetLocalFrame(const PlanetConfig& config)
{
    const GeodeticCoord origin = normalizeGeodetic(config.localOrigin);
    const double latRad = origin.latitudeDeg * (static_cast<double>(kPi) / 180.0);
    const double lonRad = origin.longitudeDeg * (static_cast<double>(kPi) / 180.0);
    const double cosLat = std::cos(latRad);
    const double sinLat = std::sin(latRad);
    const double cosLon = std::cos(lonRad);
    const double sinLon = std::sin(lonRad);

    const DVec3 up {
        cosLat * cosLon,
        sinLat,
        cosLat * sinLon
    };
    const DVec3 east {
        -sinLon,
        0.0,
        cosLon
    };
    const DVec3 north {
        -sinLat * cosLon,
        cosLat,
        -sinLat * sinLon
    };
    const DVec3 surfaceOrigin = planetFixedFromGeodetic(origin, config);
    return {
        surfaceOrigin,
        east,
        up,
        north,
        { 0.0, 1.0, 0.0 },
        config.radiusMeters + origin.altitudeMeters
    };
}

inline DVec3 planetLocalToInertial(const Vec3& localPosition, const PlanetLocalFrame& frame)
{
    return frame.surfaceOriginInertial +
        (frame.east * static_cast<double>(localPosition.x)) +
        (frame.up * static_cast<double>(localPosition.y)) +
        (frame.north * static_cast<double>(localPosition.z));
}

inline Vec3 inertialToPlanetLocal(const DVec3& inertialPosition, const PlanetLocalFrame& frame)
{
    const DVec3 relative = inertialPosition - frame.surfaceOriginInertial;
    return {
        static_cast<float>(dot(relative, frame.east)),
        static_cast<float>(dot(relative, frame.up)),
        static_cast<float>(dot(relative, frame.north))
    };
}

inline DVec3 inertialToPlanetFixed(const DVec3& inertialPosition, const PlanetConfig& config, double timeSeconds)
{
    return rotateAroundPlanetSpin(inertialPosition, -(config.rotationRateRadPerSec * timeSeconds));
}

inline DVec3 planetFixedToInertial(const DVec3& fixedPosition, const PlanetConfig& config, double timeSeconds)
{
    return rotateAroundPlanetSpin(fixedPosition, config.rotationRateRadPerSec * timeSeconds);
}

inline DVec3 planetRadialVectorLocal(const Vec3& localPosition, const PlanetLocalFrame& frame)
{
    return {
        static_cast<double>(localPosition.x),
        frame.originRadiusMeters + static_cast<double>(localPosition.y),
        static_cast<double>(localPosition.z)
    };
}

inline double planetRadialDistanceLocal(const Vec3& localPosition, const PlanetLocalFrame& frame)
{
    return length(planetRadialVectorLocal(localPosition, frame));
}

inline Vec3 planetCenterLocal(const PlanetLocalFrame& frame)
{
    return { 0.0f, static_cast<float>(-frame.originRadiusMeters), 0.0f };
}

inline Vec3 planetSpinAxisLocal(const PlanetLocalFrame& frame)
{
    const DVec3 globalAxis { 0.0, 1.0, 0.0 };
    return normalize(toVec3({
        dot(globalAxis, frame.east),
        dot(globalAxis, frame.up),
        dot(globalAxis, frame.north)
    }), { 0.0f, 1.0f, 0.0f });
}

inline PlanetTileId cubeSphereTileForDirection(const DVec3& directionInput, int lod)
{
    const DVec3 direction = normalize(directionInput, { 0.0, 1.0, 0.0 });
    const DVec3 absDir { std::fabs(direction.x), std::fabs(direction.y), std::fabs(direction.z) };
    int face = 0;
    double u = 0.0;
    double v = 0.0;

    if (absDir.x >= absDir.y && absDir.x >= absDir.z) {
        if (direction.x >= 0.0) {
            face = 0;
            u = -direction.z / std::max(1.0e-9, absDir.x);
            v = direction.y / std::max(1.0e-9, absDir.x);
        } else {
            face = 1;
            u = direction.z / std::max(1.0e-9, absDir.x);
            v = direction.y / std::max(1.0e-9, absDir.x);
        }
    } else if (absDir.y >= absDir.z) {
        if (direction.y >= 0.0) {
            face = 2;
            u = direction.x / std::max(1.0e-9, absDir.y);
            v = -direction.z / std::max(1.0e-9, absDir.y);
        } else {
            face = 3;
            u = direction.x / std::max(1.0e-9, absDir.y);
            v = direction.z / std::max(1.0e-9, absDir.y);
        }
    } else {
        if (direction.z >= 0.0) {
            face = 4;
            u = direction.x / std::max(1.0e-9, absDir.z);
            v = direction.y / std::max(1.0e-9, absDir.z);
        } else {
            face = 5;
            u = -direction.x / std::max(1.0e-9, absDir.z);
            v = direction.y / std::max(1.0e-9, absDir.z);
        }
    }

    const int tilesPerAxis = std::max(1, 1 << std::clamp(lod, 0, 24));
    const int tx = std::clamp(static_cast<int>(std::floor(((u * 0.5) + 0.5) * tilesPerAxis)), 0, tilesPerAxis - 1);
    const int ty = std::clamp(static_cast<int>(std::floor(((v * 0.5) + 0.5) * tilesPerAxis)), 0, tilesPerAxis - 1);
    return { face, std::clamp(lod, 0, 24), tx, ty };
}

}  // namespace NativeGame
