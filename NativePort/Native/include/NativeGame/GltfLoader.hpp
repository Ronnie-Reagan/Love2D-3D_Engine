#pragma once

#include "NativeGame/StlLoader.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace NativeGame {

namespace GltfDetail {

struct JsonValue {
    enum class Type {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object
    };

    Type type = Type::Null;
    bool boolValue = false;
    double numberValue = 0.0;
    std::string stringValue;
    std::vector<JsonValue> arrayValue;
    std::unordered_map<std::string, JsonValue> objectValue;
};

class JsonParser {
public:
    explicit JsonParser(const std::string& text);

    bool parse(JsonValue& outValue, std::string* error);

private:
    const std::string& text_;
    std::size_t position_ = 0;

    bool fail(std::string* error, const std::string& message);
    void skipWhitespace();
    bool parseValue(JsonValue& outValue, std::string* error);
    bool parseString(std::string& outValue, std::string* error);
    bool parseNumber(double& outValue, std::string* error);
    bool parseArray(JsonValue& outValue, std::string* error);
    bool parseObject(JsonValue& outValue, std::string* error);
};

const JsonValue* objectMember(const JsonValue& object, std::string_view key);
const JsonValue* arrayElement(const JsonValue& array, std::size_t index);
bool asBool(const JsonValue* value, bool fallback);
double asNumber(const JsonValue* value, double fallback);
int asInt(const JsonValue* value, int fallback);
std::string asString(const JsonValue* value, const std::string& fallback = {});

std::vector<std::uint8_t> readFileBytes(const std::filesystem::path& path);
bool hasUriScheme(const std::string& uri);
std::optional<std::filesystem::path> resolveRelativePath(
    const std::filesystem::path& baseDirectory,
    const std::string& uri,
    std::string* error);
std::vector<std::uint8_t> decodeBase64(std::string_view text, std::string* error);
std::vector<std::uint8_t> decodeDataUri(std::string_view uri, std::string* error);
std::uint16_t readU16LE(const std::vector<std::uint8_t>& bytes, std::size_t offset);
std::int16_t readS16LE(const std::vector<std::uint8_t>& bytes, std::size_t offset);
std::uint32_t readU32LE(const std::vector<std::uint8_t>& bytes, std::size_t offset);
float readF32LE(const std::vector<std::uint8_t>& bytes, std::size_t offset);

struct Mat4 {
    std::array<float, 16> m {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
};

Mat4 multiply(const Mat4& lhs, const Mat4& rhs);
Mat4 makeTrsMatrix(const Vec3& translation, const Quat& rotation, const Vec3& scale);
Vec3 transformPosition(const Mat4& matrix, const Vec3& position);
Vec3 transformDirection(const Mat4& matrix, const Vec3& direction);
float determinant3x3(const Mat4& matrix);

bool decodeGlbChunks(
    const std::vector<std::uint8_t>& bytes,
    std::string& jsonChunk,
    std::vector<std::uint8_t>& binChunk,
    std::string* error);
bool rejectUnsupportedExtensions(const JsonValue& gltf, std::string* error);

struct AccessorData {
    int components = 0;
    int count = 0;
    std::vector<double> values;
};

int componentByteSize(int componentType);
int componentCountForType(const std::string& type);
double readAccessorComponent(
    const std::vector<std::uint8_t>& bytes,
    std::size_t offset,
    int componentType,
    bool normalized,
    bool* ok);
bool decodeAccessor(
    const JsonValue& gltf,
    const std::vector<std::vector<std::uint8_t>>& buffers,
    int accessorIndex,
    AccessorData& outData,
    std::string* error);
TextureRef resolveTextureRef(
    const JsonValue& gltf,
    const JsonValue* textureValue,
    const char* scalarField);
Material buildMaterial(const JsonValue& gltf, const JsonValue& materialValue, int materialIndex);
bool decodeImageRecord(
    const JsonValue& gltf,
    const JsonValue& imageValue,
    const std::vector<std::vector<std::uint8_t>>& buffers,
    const std::filesystem::path& baseDirectory,
    RgbaImage& image,
    std::string* error);
std::vector<Material> buildMaterials(const JsonValue& gltf);
Vec3 materialBaseColor(const JsonValue& gltf, int materialIndex);
Mat4 nodeMatrix(const JsonValue& node);
bool appendPrimitiveToModel(
    const JsonValue& gltf,
    const std::vector<std::vector<std::uint8_t>>& buffers,
    const JsonValue& primitive,
    const Mat4& worldMatrix,
    Model& model,
    std::string* error);
bool buildModelFromGltf(
    const JsonValue& gltf,
    const std::vector<std::vector<std::uint8_t>>& buffers,
    Model& model,
    std::string* error);
bool decodeGltfDocument(
    const JsonValue& gltf,
    const std::filesystem::path& baseDirectory,
    const std::vector<std::uint8_t>& primaryBin,
    Model& model,
    std::string* error);

inline JsonParser::JsonParser(const std::string& text)
    : text_(text)
{
}

inline bool JsonParser::parse(JsonValue& outValue, std::string* error)
{
    skipWhitespace();
    if (!parseValue(outValue, error)) {
        return false;
    }
    skipWhitespace();
    if (position_ != text_.size()) {
        return fail(error, "json decode error at byte " + std::to_string(position_ + 1) + ": trailing garbage");
    }
    return true;
}

inline bool JsonParser::fail(std::string* error, const std::string& message)
{
    if (error != nullptr) {
        *error = message;
    }
    return false;
}

inline void JsonParser::skipWhitespace()
{
    while (position_ < text_.size()) {
        const unsigned char ch = static_cast<unsigned char>(text_[position_]);
        if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') {
            break;
        }
        ++position_;
    }
}

inline bool JsonParser::parseValue(JsonValue& outValue, std::string* error)
{
    skipWhitespace();
    if (position_ >= text_.size()) {
        return fail(error, "json decode error at byte " + std::to_string(position_ + 1) + ": unexpected end of input");
    }

    const char ch = text_[position_];
    if (ch == '"') {
        outValue.type = JsonValue::Type::String;
        return parseString(outValue.stringValue, error);
    }
    if (ch == '{') {
        return parseObject(outValue, error);
    }
    if (ch == '[') {
        return parseArray(outValue, error);
    }
    if (ch == '-' || (ch >= '0' && ch <= '9')) {
        outValue.type = JsonValue::Type::Number;
        return parseNumber(outValue.numberValue, error);
    }
    if (text_.compare(position_, 4, "true") == 0) {
        outValue.type = JsonValue::Type::Bool;
        outValue.boolValue = true;
        position_ += 4;
        return true;
    }
    if (text_.compare(position_, 5, "false") == 0) {
        outValue.type = JsonValue::Type::Bool;
        outValue.boolValue = false;
        position_ += 5;
        return true;
    }
    if (text_.compare(position_, 4, "null") == 0) {
        outValue = {};
        position_ += 4;
        return true;
    }
    return fail(error, "json decode error at byte " + std::to_string(position_ + 1) + ": unexpected token");
}

inline bool JsonParser::parseString(std::string& outValue, std::string* error)
{
    if (position_ >= text_.size() || text_[position_] != '"') {
        return fail(error, "json decode error at byte " + std::to_string(position_ + 1) + ": expected string");
    }

    ++position_;
    outValue.clear();
    while (position_ < text_.size()) {
        const char ch = text_[position_++];
        if (ch == '"') {
            return true;
        }
        if (ch == '\\') {
            if (position_ >= text_.size()) {
                return fail(error, "json decode error at byte " + std::to_string(position_ + 1) + ": unterminated escape");
            }

            const char esc = text_[position_++];
            switch (esc) {
            case '"':
            case '\\':
            case '/':
                outValue.push_back(esc);
                break;
            case 'b':
                outValue.push_back('\b');
                break;
            case 'f':
                outValue.push_back('\f');
                break;
            case 'n':
                outValue.push_back('\n');
                break;
            case 'r':
                outValue.push_back('\r');
                break;
            case 't':
                outValue.push_back('\t');
                break;
            case 'u': {
                if (position_ + 4 > text_.size()) {
                    return fail(error, "json decode error at byte " + std::to_string(position_) + ": invalid unicode escape");
                }

                const std::string hex = text_.substr(position_, 4);
                position_ += 4;
                char* end = nullptr;
                const long codePoint = std::strtol(hex.c_str(), &end, 16);
                if (end == hex.c_str() || *end != '\0') {
                    return fail(error, "json decode error at byte " + std::to_string(position_ - 3) + ": invalid unicode escape");
                }

                if (codePoint <= 0x7F) {
                    outValue.push_back(static_cast<char>(codePoint));
                } else if (codePoint <= 0x7FF) {
                    outValue.push_back(static_cast<char>(0xC0 | ((codePoint >> 6) & 0x1F)));
                    outValue.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
                } else {
                    outValue.push_back(static_cast<char>(0xE0 | ((codePoint >> 12) & 0x0F)));
                    outValue.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
                    outValue.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
                }
                break;
            }
            default:
                return fail(error, "json decode error at byte " + std::to_string(position_) + ": unknown escape");
            }
            continue;
        }
        outValue.push_back(ch);
    }

    return fail(error, "json decode error at byte " + std::to_string(position_ + 1) + ": unterminated string");
}

inline bool JsonParser::parseNumber(double& outValue, std::string* error)
{
    const char* start = text_.c_str() + position_;
    char* end = nullptr;
    errno = 0;
    const double parsed = std::strtod(start, &end);
    if (end == start) {
        return fail(error, "json decode error at byte " + std::to_string(position_ + 1) + ": invalid number");
    }

    position_ += static_cast<std::size_t>(end - start);
    outValue = parsed;
    return true;
}

inline bool JsonParser::parseArray(JsonValue& outValue, std::string* error)
{
    ++position_;
    outValue = {};
    outValue.type = JsonValue::Type::Array;
    skipWhitespace();
    if (position_ < text_.size() && text_[position_] == ']') {
        ++position_;
        return true;
    }

    while (position_ < text_.size()) {
        JsonValue element;
        if (!parseValue(element, error)) {
            return false;
        }
        outValue.arrayValue.push_back(std::move(element));
        skipWhitespace();
        if (position_ >= text_.size()) {
            break;
        }
        if (text_[position_] == ']') {
            ++position_;
            return true;
        }
        if (text_[position_] != ',') {
            return fail(error, "json decode error at byte " + std::to_string(position_ + 1) + ": expected ',' or ']'");
        }
        ++position_;
        skipWhitespace();
    }

    return fail(error, "json decode error at byte " + std::to_string(position_ + 1) + ": unterminated array");
}

inline bool JsonParser::parseObject(JsonValue& outValue, std::string* error)
{
    ++position_;
    outValue = {};
    outValue.type = JsonValue::Type::Object;
    skipWhitespace();
    if (position_ < text_.size() && text_[position_] == '}') {
        ++position_;
        return true;
    }

    while (position_ < text_.size()) {
        std::string key;
        if (!parseString(key, error)) {
            return false;
        }
        skipWhitespace();
        if (position_ >= text_.size() || text_[position_] != ':') {
            return fail(error, "json decode error at byte " + std::to_string(position_ + 1) + ": expected ':'");
        }
        ++position_;
        skipWhitespace();
        JsonValue value;
        if (!parseValue(value, error)) {
            return false;
        }
        outValue.objectValue.emplace(std::move(key), std::move(value));
        skipWhitespace();
        if (position_ >= text_.size()) {
            break;
        }
        if (text_[position_] == '}') {
            ++position_;
            return true;
        }
        if (text_[position_] != ',') {
            return fail(error, "json decode error at byte " + std::to_string(position_ + 1) + ": expected ',' or '}'");
        }
        ++position_;
        skipWhitespace();
    }

    return fail(error, "json decode error at byte " + std::to_string(position_ + 1) + ": unterminated object");
}

inline const JsonValue* objectMember(const JsonValue& object, const std::string_view key)
{
    if (object.type != JsonValue::Type::Object) {
        return nullptr;
    }
    const auto it = object.objectValue.find(std::string(key));
    return it == object.objectValue.end() ? nullptr : &it->second;
}

inline const JsonValue* arrayElement(const JsonValue& array, const std::size_t index)
{
    if (array.type != JsonValue::Type::Array || index >= array.arrayValue.size()) {
        return nullptr;
    }
    return &array.arrayValue[index];
}

inline bool asBool(const JsonValue* value, const bool fallback)
{
    return (value != nullptr && value->type == JsonValue::Type::Bool) ? value->boolValue : fallback;
}

inline double asNumber(const JsonValue* value, const double fallback)
{
    return (value != nullptr && value->type == JsonValue::Type::Number) ? value->numberValue : fallback;
}

inline int asInt(const JsonValue* value, const int fallback)
{
    if (value == nullptr || value->type != JsonValue::Type::Number) {
        return fallback;
    }
    return static_cast<int>(std::llround(value->numberValue));
}

inline std::string asString(const JsonValue* value, const std::string& fallback)
{
    return (value != nullptr && value->type == JsonValue::Type::String) ? value->stringValue : fallback;
}

inline std::vector<std::uint8_t> readFileBytes(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {};
    }

    return std::vector<std::uint8_t>(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

inline bool hasUriScheme(const std::string& uri)
{
    const std::size_t colon = uri.find(':');
    const std::size_t slash = uri.find('/');
    const std::size_t backslash = uri.find('\\');
    const std::size_t limit = std::min(
        slash == std::string::npos ? uri.size() : slash,
        backslash == std::string::npos ? uri.size() : backslash);
    return colon != std::string::npos && colon < limit;
}

inline std::optional<std::filesystem::path> resolveRelativePath(
    const std::filesystem::path& baseDirectory,
    const std::string& uri,
    std::string* error)
{
    if (uri.empty()) {
        if (error != nullptr) {
            *error = "missing uri";
        }
        return std::nullopt;
    }
    if (uri.rfind("data:", 0) == 0) {
        if (error != nullptr) {
            *error = "data uri should not be resolved as a file";
        }
        return std::nullopt;
    }
    if (std::filesystem::path(uri).is_absolute() || hasUriScheme(uri)) {
        if (error != nullptr) {
            *error = "absolute and non-file uris are not supported";
        }
        return std::nullopt;
    }

    return (baseDirectory / std::filesystem::path(uri)).lexically_normal();
}

inline std::vector<std::uint8_t> decodeBase64(std::string_view text, std::string* error)
{
    std::array<int, 256> index {};
    index.fill(-1);
    const std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (std::size_t i = 0; i < alphabet.size(); ++i) {
        index[static_cast<unsigned char>(alphabet[i])] = static_cast<int>(i);
    }

    std::string compact;
    compact.reserve(text.size());
    for (const char ch : text) {
        if (!std::isspace(static_cast<unsigned char>(ch))) {
            compact.push_back(ch);
        }
    }

    std::vector<std::uint8_t> out;
    out.reserve((compact.size() * 3u) / 4u);
    for (std::size_t i = 0; i + 3 < compact.size(); i += 4) {
        const char c1 = compact[i];
        const char c2 = compact[i + 1];
        const char c3 = compact[i + 2];
        const char c4 = compact[i + 3];
        const int v1 = index[static_cast<unsigned char>(c1)];
        const int v2 = index[static_cast<unsigned char>(c2)];
        const int v3 = c3 == '=' ? 0 : index[static_cast<unsigned char>(c3)];
        const int v4 = c4 == '=' ? 0 : index[static_cast<unsigned char>(c4)];
        if (v1 < 0 || v2 < 0 || (c3 != '=' && v3 < 0) || (c4 != '=' && v4 < 0)) {
            if (error != nullptr) {
                *error = "invalid base64 payload";
            }
            return {};
        }

        const std::uint32_t packed =
            (static_cast<std::uint32_t>(v1) << 18) |
            (static_cast<std::uint32_t>(v2) << 12) |
            (static_cast<std::uint32_t>(v3) << 6) |
            static_cast<std::uint32_t>(v4);
        out.push_back(static_cast<std::uint8_t>((packed >> 16) & 0xFFu));
        if (c3 != '=') {
            out.push_back(static_cast<std::uint8_t>((packed >> 8) & 0xFFu));
        }
        if (c4 != '=') {
            out.push_back(static_cast<std::uint8_t>(packed & 0xFFu));
        }
    }

    return out;
}

inline std::vector<std::uint8_t> decodeDataUri(std::string_view uri, std::string* error)
{
    constexpr std::string_view prefix = "data:";
    if (!uri.starts_with(prefix)) {
        if (error != nullptr) {
            *error = "unsupported data uri";
        }
        return {};
    }

    const std::size_t comma = uri.find(',');
    if (comma == std::string_view::npos) {
        if (error != nullptr) {
            *error = "malformed data uri";
        }
        return {};
    }

    const std::string_view header = uri.substr(prefix.size(), comma - prefix.size());
    const std::string_view payload = uri.substr(comma + 1);
    if (header.find(";base64") == std::string_view::npos) {
        if (error != nullptr) {
            *error = "non-base64 data uris are not supported";
        }
        return {};
    }
    return decodeBase64(payload, error);
}

inline std::uint16_t readU16LE(const std::vector<std::uint8_t>& bytes, const std::size_t offset)
{
    return static_cast<std::uint16_t>(bytes[offset]) |
        (static_cast<std::uint16_t>(bytes[offset + 1]) << 8);
}

inline std::int16_t readS16LE(const std::vector<std::uint8_t>& bytes, const std::size_t offset)
{
    return static_cast<std::int16_t>(readU16LE(bytes, offset));
}

inline std::uint32_t readU32LE(const std::vector<std::uint8_t>& bytes, const std::size_t offset)
{
    return static_cast<std::uint32_t>(bytes[offset]) |
        (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
        (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
        (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

inline float readF32LE(const std::vector<std::uint8_t>& bytes, const std::size_t offset)
{
    const std::uint32_t raw = readU32LE(bytes, offset);
    return std::bit_cast<float>(raw);
}

inline Mat4 multiply(const Mat4& lhs, const Mat4& rhs)
{
    Mat4 result {};
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            result.m[static_cast<std::size_t>((col * 4) + row)] =
                lhs.m[static_cast<std::size_t>(row)] * rhs.m[static_cast<std::size_t>(col * 4)] +
                lhs.m[static_cast<std::size_t>(row + 4)] * rhs.m[static_cast<std::size_t>((col * 4) + 1)] +
                lhs.m[static_cast<std::size_t>(row + 8)] * rhs.m[static_cast<std::size_t>((col * 4) + 2)] +
                lhs.m[static_cast<std::size_t>(row + 12)] * rhs.m[static_cast<std::size_t>((col * 4) + 3)];
        }
    }
    return result;
}

inline Mat4 makeTrsMatrix(const Vec3& translation, const Quat& rotation, const Vec3& scale)
{
    const float x = rotation.x;
    const float y = rotation.y;
    const float z = rotation.z;
    const float w = rotation.w;
    const float xx = x * x;
    const float yy = y * y;
    const float zz = z * z;
    const float xy = x * y;
    const float xz = x * z;
    const float yz = y * z;
    const float wx = w * x;
    const float wy = w * y;
    const float wz = w * z;

    Mat4 result {};
    result.m = {
        (1.0f - (2.0f * (yy + zz))) * scale.x,
        (2.0f * (xy + wz)) * scale.x,
        (2.0f * (xz - wy)) * scale.x,
        0.0f,

        (2.0f * (xy - wz)) * scale.y,
        (1.0f - (2.0f * (xx + zz))) * scale.y,
        (2.0f * (yz + wx)) * scale.y,
        0.0f,

        (2.0f * (xz + wy)) * scale.z,
        (2.0f * (yz - wx)) * scale.z,
        (1.0f - (2.0f * (xx + yy))) * scale.z,
        0.0f,

        translation.x,
        translation.y,
        translation.z,
        1.0f
    };
    return result;
}

inline Vec3 transformPosition(const Mat4& matrix, const Vec3& position)
{
    return {
        (matrix.m[0] * position.x) + (matrix.m[4] * position.y) + (matrix.m[8] * position.z) + matrix.m[12],
        (matrix.m[1] * position.x) + (matrix.m[5] * position.y) + (matrix.m[9] * position.z) + matrix.m[13],
        (matrix.m[2] * position.x) + (matrix.m[6] * position.y) + (matrix.m[10] * position.z) + matrix.m[14]
    };
}

inline Vec3 transformDirection(const Mat4& matrix, const Vec3& direction)
{
    return normalize({
        (matrix.m[0] * direction.x) + (matrix.m[4] * direction.y) + (matrix.m[8] * direction.z),
        (matrix.m[1] * direction.x) + (matrix.m[5] * direction.y) + (matrix.m[9] * direction.z),
        (matrix.m[2] * direction.x) + (matrix.m[6] * direction.y) + (matrix.m[10] * direction.z)
    }, { 0.0f, 1.0f, 0.0f });
}

inline float determinant3x3(const Mat4& matrix)
{
    const float a11 = matrix.m[0];
    const float a12 = matrix.m[4];
    const float a13 = matrix.m[8];
    const float a21 = matrix.m[1];
    const float a22 = matrix.m[5];
    const float a23 = matrix.m[9];
    const float a31 = matrix.m[2];
    const float a32 = matrix.m[6];
    const float a33 = matrix.m[10];
    return (a11 * ((a22 * a33) - (a23 * a32))) -
        (a12 * ((a21 * a33) - (a23 * a31))) +
        (a13 * ((a21 * a32) - (a22 * a31)));
}

inline bool decodeGlbChunks(
    const std::vector<std::uint8_t>& bytes,
    std::string& jsonChunk,
    std::vector<std::uint8_t>& binChunk,
    std::string* error)
{
    if (bytes.size() < 20) {
        if (error != nullptr) {
            *error = "GLB header too short";
        }
        return false;
    }
    if (readU32LE(bytes, 0) != 0x46546C67u) {
        if (error != nullptr) {
            *error = "invalid GLB magic";
        }
        return false;
    }
    if (readU32LE(bytes, 4) != 2u) {
        if (error != nullptr) {
            *error = "unsupported GLB version";
        }
        return false;
    }
    if (readU32LE(bytes, 8) != bytes.size()) {
        if (error != nullptr) {
            *error = "GLB length mismatch";
        }
        return false;
    }

    std::size_t cursor = 12;
    while (cursor + 8 <= bytes.size()) {
        const std::uint32_t chunkLength = readU32LE(bytes, cursor);
        const std::uint32_t chunkType = readU32LE(bytes, cursor + 4);
        const std::size_t chunkStart = cursor + 8;
        const std::size_t chunkEnd = chunkStart + static_cast<std::size_t>(chunkLength);
        if (chunkEnd > bytes.size()) {
            if (error != nullptr) {
                *error = "GLB chunk exceeds payload bounds";
            }
            return false;
        }

        if (chunkType == 0x4E4F534Au) {
            jsonChunk.assign(bytes.begin() + static_cast<std::ptrdiff_t>(chunkStart), bytes.begin() + static_cast<std::ptrdiff_t>(chunkEnd));
        } else if (chunkType == 0x004E4942u) {
            binChunk.assign(bytes.begin() + static_cast<std::ptrdiff_t>(chunkStart), bytes.begin() + static_cast<std::ptrdiff_t>(chunkEnd));
        }
        cursor = chunkEnd;
    }

    while (!jsonChunk.empty()) {
        const unsigned char ch = static_cast<unsigned char>(jsonChunk.back());
        if (ch != 0 && !std::isspace(ch)) {
            break;
        }
        jsonChunk.pop_back();
    }

    if (jsonChunk.empty()) {
        if (error != nullptr) {
            *error = "GLB missing JSON chunk";
        }
        return false;
    }

    return true;
}

inline bool rejectUnsupportedExtensions(const JsonValue& gltf, std::string* error)
{
    static const std::array<std::string_view, 3> unsupported {
        "KHR_draco_mesh_compression",
        "EXT_meshopt_compression",
        "KHR_texture_basisu"
    };

    const auto checkArray = [&](const JsonValue* value, const std::string_view label) -> bool {
        if (value == nullptr || value->type != JsonValue::Type::Array) {
            return true;
        }
        for (const JsonValue& entry : value->arrayValue) {
            if (entry.type != JsonValue::Type::String) {
                continue;
            }
            for (const std::string_view unsupportedName : unsupported) {
                if (entry.stringValue == unsupportedName) {
                    if (error != nullptr) {
                        *error = std::string(label) + " unsupported glTF extension: " + entry.stringValue;
                    }
                    return false;
                }
            }
        }
        return true;
    };

    return checkArray(objectMember(gltf, "extensionsUsed"), "uses") &&
        checkArray(objectMember(gltf, "extensionsRequired"), "requires");
}

inline int componentByteSize(const int componentType)
{
    switch (componentType) {
    case 5120:
    case 5121:
        return 1;
    case 5122:
    case 5123:
        return 2;
    case 5125:
    case 5126:
        return 4;
    default:
        return 0;
    }
}

inline int componentCountForType(const std::string& type)
{
    if (type == "SCALAR") {
        return 1;
    }
    if (type == "VEC2") {
        return 2;
    }
    if (type == "VEC3") {
        return 3;
    }
    if (type == "VEC4") {
        return 4;
    }
    if (type == "MAT4") {
        return 16;
    }
    return 0;
}

inline double readAccessorComponent(
    const std::vector<std::uint8_t>& bytes,
    const std::size_t offset,
    const int componentType,
    const bool normalized,
    bool* ok)
{
    *ok = true;
    switch (componentType) {
    case 5120: {
        const auto value = static_cast<std::int8_t>(bytes[offset]);
        return normalized ? std::max(-1.0, static_cast<double>(value) / 127.0) : static_cast<double>(value);
    }
    case 5121: {
        const auto value = bytes[offset];
        return normalized ? static_cast<double>(value) / 255.0 : static_cast<double>(value);
    }
    case 5122: {
        const auto value = readS16LE(bytes, offset);
        return normalized ? std::max(-1.0, static_cast<double>(value) / 32767.0) : static_cast<double>(value);
    }
    case 5123: {
        const auto value = readU16LE(bytes, offset);
        return normalized ? static_cast<double>(value) / 65535.0 : static_cast<double>(value);
    }
    case 5125:
        return static_cast<double>(readU32LE(bytes, offset));
    case 5126:
        return static_cast<double>(readF32LE(bytes, offset));
    default:
        *ok = false;
        return 0.0;
    }
}

inline bool decodeAccessor(
    const JsonValue& gltf,
    const std::vector<std::vector<std::uint8_t>>& buffers,
    const int accessorIndex,
    AccessorData& outData,
    std::string* error)
{
    const JsonValue* accessors = objectMember(gltf, "accessors");
    const JsonValue* bufferViews = objectMember(gltf, "bufferViews");
    if (accessors == nullptr || bufferViews == nullptr) {
        if (error != nullptr) {
            *error = "glTF is missing accessors or bufferViews";
        }
        return false;
    }

    const JsonValue* accessor = arrayElement(*accessors, static_cast<std::size_t>(accessorIndex));
    if (accessor == nullptr || accessor->type != JsonValue::Type::Object) {
        if (error != nullptr) {
            *error = "invalid glTF accessor index";
        }
        return false;
    }

    const int bufferViewIndex = asInt(objectMember(*accessor, "bufferView"), -1);
    if (bufferViewIndex < 0) {
        if (error != nullptr) {
            *error = "sparse and missing-bufferView glTF accessors are not supported";
        }
        return false;
    }

    const JsonValue* bufferView = arrayElement(*bufferViews, static_cast<std::size_t>(bufferViewIndex));
    if (bufferView == nullptr || bufferView->type != JsonValue::Type::Object) {
        if (error != nullptr) {
            *error = "invalid glTF bufferView index";
        }
        return false;
    }

    const int bufferIndex = asInt(objectMember(*bufferView, "buffer"), -1);
    if (bufferIndex < 0 || static_cast<std::size_t>(bufferIndex) >= buffers.size()) {
        if (error != nullptr) {
            *error = "glTF bufferView references an invalid buffer";
        }
        return false;
    }

    const int componentType = asInt(objectMember(*accessor, "componentType"), 0);
    const int components = componentCountForType(asString(objectMember(*accessor, "type")));
    const int count = asInt(objectMember(*accessor, "count"), 0);
    const int bytesPerComponent = componentByteSize(componentType);
    if (components <= 0 || count <= 0 || bytesPerComponent <= 0) {
        if (error != nullptr) {
            *error = "unsupported glTF accessor format";
        }
        return false;
    }

    const std::size_t elementBytes = static_cast<std::size_t>(components * bytesPerComponent);
    const std::size_t stride = std::max<std::size_t>(
        elementBytes,
        static_cast<std::size_t>(std::max(0, asInt(objectMember(*bufferView, "byteStride"), 0))));
    const std::size_t startOffset =
        static_cast<std::size_t>(std::max(0, asInt(objectMember(*bufferView, "byteOffset"), 0))) +
        static_cast<std::size_t>(std::max(0, asInt(objectMember(*accessor, "byteOffset"), 0)));
    const bool normalized = asBool(objectMember(*accessor, "normalized"), false);
    const std::vector<std::uint8_t>& sourceBytes = buffers[static_cast<std::size_t>(bufferIndex)];

    if (startOffset + (static_cast<std::size_t>(count - 1) * stride) + elementBytes > sourceBytes.size()) {
        if (error != nullptr) {
            *error = "glTF accessor exceeds buffer bounds";
        }
        return false;
    }

    outData.components = components;
    outData.count = count;
    outData.values.clear();
    outData.values.reserve(static_cast<std::size_t>(count * components));
    for (int elementIndex = 0; elementIndex < count; ++elementIndex) {
        const std::size_t elementOffset = startOffset + (static_cast<std::size_t>(elementIndex) * stride);
        for (int componentIndex = 0; componentIndex < components; ++componentIndex) {
            bool ok = false;
            const double value = readAccessorComponent(
                sourceBytes,
                elementOffset + static_cast<std::size_t>(componentIndex * bytesPerComponent),
                componentType,
                normalized,
                &ok);
            if (!ok) {
                if (error != nullptr) {
                    *error = "unsupported glTF accessor component type";
                }
                return false;
            }
            outData.values.push_back(value);
        }
    }

    return true;
}

inline TextureRef resolveTextureRef(
    const JsonValue& gltf,
    const JsonValue* textureValue,
    const char* scalarField)
{
    TextureRef result {};
    if (textureValue == nullptr || textureValue->type != JsonValue::Type::Object) {
        return result;
    }

    const int textureIndex = asInt(objectMember(*textureValue, "index"), -1);
    if (textureIndex < 0) {
        return result;
    }

    const JsonValue* textures = objectMember(gltf, "textures");
    const JsonValue* textureDef =
        textures != nullptr ? arrayElement(*textures, static_cast<std::size_t>(textureIndex)) : nullptr;
    if (textureDef == nullptr || textureDef->type != JsonValue::Type::Object) {
        return result;
    }

    result.textureIndex = textureIndex;
    result.imageIndex = asInt(objectMember(*textureDef, "source"), -1);
    result.texCoord = std::max(0, asInt(objectMember(*textureValue, "texCoord"), 0));
    if (scalarField != nullptr && *scalarField != '\0') {
        const float scalar = static_cast<float>(asNumber(objectMember(*textureValue, scalarField), 1.0));
        if (std::string_view(scalarField) == "strength") {
            result.strength = scalar;
        } else {
            result.scale = scalar;
        }
    }
    return result;
}

inline Material buildMaterial(const JsonValue& gltf, const JsonValue& materialValue, const int materialIndex)
{
    Material material {};
    material.name = asString(objectMember(materialValue, "name"), "material_" + std::to_string(materialIndex));

    const JsonValue* pbr = objectMember(materialValue, "pbrMetallicRoughness");
    const JsonValue* baseColorFactor = pbr != nullptr ? objectMember(*pbr, "baseColorFactor") : nullptr;
    if (baseColorFactor != nullptr &&
        baseColorFactor->type == JsonValue::Type::Array &&
        baseColorFactor->arrayValue.size() >= 4) {
        material.baseColorFactor = {
            static_cast<float>(asNumber(arrayElement(*baseColorFactor, 0), 1.0)),
            static_cast<float>(asNumber(arrayElement(*baseColorFactor, 1), 1.0)),
            static_cast<float>(asNumber(arrayElement(*baseColorFactor, 2), 1.0)),
            static_cast<float>(asNumber(arrayElement(*baseColorFactor, 3), 1.0))
        };
    }

    material.metallicFactor = static_cast<float>(asNumber(
        pbr != nullptr ? objectMember(*pbr, "metallicFactor") : nullptr,
        1.0));
    material.roughnessFactor = static_cast<float>(asNumber(
        pbr != nullptr ? objectMember(*pbr, "roughnessFactor") : nullptr,
        1.0));
    material.baseColorTexture = resolveTextureRef(gltf, pbr != nullptr ? objectMember(*pbr, "baseColorTexture") : nullptr, "");
    material.metallicRoughnessTexture =
        resolveTextureRef(gltf, pbr != nullptr ? objectMember(*pbr, "metallicRoughnessTexture") : nullptr, "");
    material.normalTexture = resolveTextureRef(gltf, objectMember(materialValue, "normalTexture"), "scale");
    material.occlusionTexture = resolveTextureRef(gltf, objectMember(materialValue, "occlusionTexture"), "strength");
    material.emissiveTexture = resolveTextureRef(gltf, objectMember(materialValue, "emissiveTexture"), "");

    const JsonValue* emissiveFactor = objectMember(materialValue, "emissiveFactor");
    if (emissiveFactor != nullptr &&
        emissiveFactor->type == JsonValue::Type::Array &&
        emissiveFactor->arrayValue.size() >= 3) {
        material.emissiveFactor = {
            static_cast<float>(asNumber(arrayElement(*emissiveFactor, 0), 0.0)),
            static_cast<float>(asNumber(arrayElement(*emissiveFactor, 1), 0.0)),
            static_cast<float>(asNumber(arrayElement(*emissiveFactor, 2), 0.0))
        };
    }

    const std::string alphaMode = asString(objectMember(materialValue, "alphaMode"), "OPAQUE");
    if (alphaMode == "MASK") {
        material.alphaMode = AlphaMode::Mask;
    } else if (alphaMode == "BLEND") {
        material.alphaMode = AlphaMode::Blend;
    }
    material.alphaCutoff = static_cast<float>(asNumber(objectMember(materialValue, "alphaCutoff"), 0.5));
    material.doubleSided = asBool(objectMember(materialValue, "doubleSided"), false);
    return material;
}

inline bool decodeImageRecord(
    const JsonValue& gltf,
    const JsonValue& imageValue,
    const std::vector<std::vector<std::uint8_t>>& buffers,
    const std::filesystem::path& baseDirectory,
    RgbaImage& image,
    std::string* error)
{
    image = {};
    if (imageValue.type != JsonValue::Type::Object) {
        if (error != nullptr) {
            *error = "invalid glTF image entry";
        }
        return false;
    }

    std::vector<std::uint8_t> encodedBytes;
    const JsonValue* uriValue = objectMember(imageValue, "uri");
    if (uriValue != nullptr && uriValue->type == JsonValue::Type::String) {
        if (uriValue->stringValue.rfind("data:", 0) == 0) {
            encodedBytes = decodeDataUri(uriValue->stringValue, error);
        } else {
            const auto resolvedPath = resolveRelativePath(baseDirectory, uriValue->stringValue, error);
            if (!resolvedPath) {
                return false;
            }
            encodedBytes = readFileBytes(*resolvedPath);
        }
    } else {
        const int bufferViewIndex = asInt(objectMember(imageValue, "bufferView"), -1);
        const JsonValue* bufferViews = objectMember(gltf, "bufferViews");
        const JsonValue* bufferView =
            bufferViews != nullptr && bufferViewIndex >= 0
                ? arrayElement(*bufferViews, static_cast<std::size_t>(bufferViewIndex))
                : nullptr;
        if (bufferView == nullptr || bufferView->type != JsonValue::Type::Object) {
            if (error != nullptr) {
                *error = "glTF image is missing uri and bufferView";
            }
            return false;
        }

        const int bufferIndex = asInt(objectMember(*bufferView, "buffer"), -1);
        if (bufferIndex < 0 || static_cast<std::size_t>(bufferIndex) >= buffers.size()) {
            if (error != nullptr) {
                *error = "glTF image buffer index is invalid";
            }
            return false;
        }

        const std::size_t byteOffset = static_cast<std::size_t>(std::max(0, asInt(objectMember(*bufferView, "byteOffset"), 0)));
        const std::size_t byteLength = static_cast<std::size_t>(std::max(0, asInt(objectMember(*bufferView, "byteLength"), 0)));
        const std::vector<std::uint8_t>& sourceBuffer = buffers[static_cast<std::size_t>(bufferIndex)];
        if (byteLength == 0 || byteOffset + byteLength > sourceBuffer.size()) {
            if (error != nullptr) {
                *error = "glTF image bufferView exceeds bounds";
            }
            return false;
        }
        encodedBytes.assign(sourceBuffer.begin() + static_cast<std::ptrdiff_t>(byteOffset),
            sourceBuffer.begin() + static_cast<std::ptrdiff_t>(byteOffset + byteLength));
    }

    if (encodedBytes.empty()) {
        if (error != nullptr) {
            *error = "glTF image payload is empty";
        }
        return false;
    }
    return decodeImageBytes(encodedBytes, image, error);
}

inline std::vector<Material> buildMaterials(const JsonValue& gltf)
{
    std::vector<Material> materials;
    const JsonValue* source = objectMember(gltf, "materials");
    if (source != nullptr && source->type == JsonValue::Type::Array) {
        materials.reserve(source->arrayValue.size());
        for (std::size_t index = 0; index < source->arrayValue.size(); ++index) {
            const JsonValue& materialValue = source->arrayValue[index];
            if (materialValue.type == JsonValue::Type::Object) {
                materials.push_back(buildMaterial(gltf, materialValue, static_cast<int>(index)));
            }
        }
    }
    if (materials.empty()) {
        materials.push_back(Material {});
    }
    return materials;
}

inline Vec3 materialBaseColor(const JsonValue& gltf, const int materialIndex)
{
    const std::vector<Material> materials = buildMaterials(gltf);
    const std::size_t index =
        materialIndex >= 0 && static_cast<std::size_t>(materialIndex) < materials.size()
            ? static_cast<std::size_t>(materialIndex)
            : 0u;
    const Vec4 color = materials[index].baseColorFactor;
    return { color.x, color.y, color.z };
}

inline Mat4 nodeMatrix(const JsonValue& node)
{
    const JsonValue* matrix = objectMember(node, "matrix");
    if (matrix != nullptr && matrix->type == JsonValue::Type::Array && matrix->arrayValue.size() == 16) {
        Mat4 out {};
        for (std::size_t i = 0; i < 16; ++i) {
            out.m[i] = static_cast<float>(asNumber(arrayElement(*matrix, i), (i % 5u) == 0u ? 1.0 : 0.0));
        }
        return out;
    }

    Vec3 translation { 0.0f, 0.0f, 0.0f };
    const JsonValue* translationArray = objectMember(node, "translation");
    if (translationArray != nullptr && translationArray->type == JsonValue::Type::Array && translationArray->arrayValue.size() >= 3) {
        translation.x = static_cast<float>(asNumber(arrayElement(*translationArray, 0), 0.0));
        translation.y = static_cast<float>(asNumber(arrayElement(*translationArray, 1), 0.0));
        translation.z = static_cast<float>(asNumber(arrayElement(*translationArray, 2), 0.0));
    }

    Quat rotation = quatIdentity();
    const JsonValue* rotationArray = objectMember(node, "rotation");
    if (rotationArray != nullptr && rotationArray->type == JsonValue::Type::Array && rotationArray->arrayValue.size() >= 4) {
        rotation = quatNormalize({
            static_cast<float>(asNumber(arrayElement(*rotationArray, 3), 1.0)),
            static_cast<float>(asNumber(arrayElement(*rotationArray, 0), 0.0)),
            static_cast<float>(asNumber(arrayElement(*rotationArray, 1), 0.0)),
            static_cast<float>(asNumber(arrayElement(*rotationArray, 2), 0.0))
        });
    }

    Vec3 scale { 1.0f, 1.0f, 1.0f };
    const JsonValue* scaleArray = objectMember(node, "scale");
    if (scaleArray != nullptr && scaleArray->type == JsonValue::Type::Array && scaleArray->arrayValue.size() >= 3) {
        scale.x = static_cast<float>(asNumber(arrayElement(*scaleArray, 0), 1.0));
        scale.y = static_cast<float>(asNumber(arrayElement(*scaleArray, 1), 1.0));
        scale.z = static_cast<float>(asNumber(arrayElement(*scaleArray, 2), 1.0));
    }

    return makeTrsMatrix(translation, rotation, scale);
}

inline bool appendPrimitiveToModel(
    const JsonValue& gltf,
    const std::vector<std::vector<std::uint8_t>>& buffers,
    const JsonValue& primitive,
    const Mat4& worldMatrix,
    Model& model,
    std::string* error)
{
    if (primitive.type != JsonValue::Type::Object) {
        if (error != nullptr) {
            *error = "invalid glTF primitive";
        }
        return false;
    }

    const int mode = asInt(objectMember(primitive, "mode"), 4);
    if (mode != 4) {
        if (error != nullptr) {
            *error = "unsupported glTF primitive mode (triangles only)";
        }
        return false;
    }

    const JsonValue* attributes = objectMember(primitive, "attributes");
    if (attributes == nullptr || attributes->type != JsonValue::Type::Object) {
        if (error != nullptr) {
            *error = "glTF primitive is missing attributes";
        }
        return false;
    }

    const int positionAccessorIndex = asInt(objectMember(*attributes, "POSITION"), -1);
    if (positionAccessorIndex < 0) {
        if (error != nullptr) {
            *error = "glTF primitive is missing POSITION";
        }
        return false;
    }

    AccessorData positions;
    if (!decodeAccessor(gltf, buffers, positionAccessorIndex, positions, error)) {
        return false;
    }
    if (positions.components < 3 || positions.count <= 0) {
        if (error != nullptr) {
            *error = "glTF POSITION accessor is invalid";
        }
        return false;
    }

    AccessorData normals;
    const int normalAccessorIndex = asInt(objectMember(*attributes, "NORMAL"), -1);
    const bool hasNormals = normalAccessorIndex >= 0 && decodeAccessor(gltf, buffers, normalAccessorIndex, normals, error);
    if (normalAccessorIndex >= 0 && !hasNormals) {
        return false;
    }
    if (hasNormals && (normals.components < 3 || normals.count != positions.count)) {
        if (error != nullptr) {
            *error = "glTF NORMAL accessor is invalid";
        }
        return false;
    }

    AccessorData texCoords;
    const int texCoordAccessorIndex = asInt(objectMember(*attributes, "TEXCOORD_0"), -1);
    const bool hasTexCoords = texCoordAccessorIndex >= 0 && decodeAccessor(gltf, buffers, texCoordAccessorIndex, texCoords, error);
    if (texCoordAccessorIndex >= 0 && !hasTexCoords) {
        return false;
    }
    if (hasTexCoords && (texCoords.components < 2 || texCoords.count != positions.count)) {
        if (error != nullptr) {
            *error = "glTF TEXCOORD_0 accessor is invalid";
        }
        return false;
    }

    const bool mirroredTransform = determinant3x3(worldMatrix) < 0.0f;
    const int baseVertex = static_cast<int>(model.vertices.size());
    model.vertices.reserve(model.vertices.size() + static_cast<std::size_t>(positions.count));
    model.vertexNormals.reserve(model.vertexNormals.size() + static_cast<std::size_t>(positions.count));
    model.texCoords.reserve(model.texCoords.size() + static_cast<std::size_t>(positions.count));
    for (int i = 0; i < positions.count; ++i) {
        const std::size_t offset = static_cast<std::size_t>(i * positions.components);
        model.vertices.push_back(transformPosition(worldMatrix, {
            static_cast<float>(positions.values[offset + 0]),
            static_cast<float>(positions.values[offset + 1]),
            static_cast<float>(positions.values[offset + 2])
        }));

        if (hasNormals) {
            const std::size_t normalOffset = static_cast<std::size_t>(i * normals.components);
            model.vertexNormals.push_back(transformDirection(worldMatrix, {
                static_cast<float>(normals.values[normalOffset + 0]),
                static_cast<float>(normals.values[normalOffset + 1]),
                static_cast<float>(normals.values[normalOffset + 2])
            }));
        } else {
            model.vertexNormals.push_back({ 0.0f, 1.0f, 0.0f });
        }

        if (hasTexCoords) {
            const std::size_t texCoordOffset = static_cast<std::size_t>(i * texCoords.components);
            model.texCoords.push_back({
                static_cast<float>(texCoords.values[texCoordOffset + 0]),
                static_cast<float>(texCoords.values[texCoordOffset + 1])
            });
            model.hasTexCoords = true;
        } else {
            model.texCoords.push_back({ 0.0f, 0.0f });
        }
    }

    const int requestedMaterialIndex = asInt(objectMember(primitive, "material"), 0);
    const int materialIndex =
        requestedMaterialIndex >= 0 && static_cast<std::size_t>(requestedMaterialIndex) < model.materials.size()
            ? requestedMaterialIndex
            : 0;
    const Material& material =
        model.materials.empty() ? Material {} : model.materials[static_cast<std::size_t>(materialIndex)];
    const Vec3 color { material.baseColorFactor.x, material.baseColorFactor.y, material.baseColorFactor.z };
    if (material.baseColorTexture.valid() &&
        static_cast<std::size_t>(material.baseColorTexture.imageIndex) < model.images.size()) {
        model.hasTextureImages = true;
        if (model.hasTexCoords) {
            model.hasPaintableMaterial = true;
        }
    }
    const int indexAccessorIndex = asInt(objectMember(primitive, "indices"), -1);
    if (indexAccessorIndex >= 0) {
        AccessorData indices;
        if (!decodeAccessor(gltf, buffers, indexAccessorIndex, indices, error)) {
            return false;
        }
        if (indices.components != 1) {
            if (error != nullptr) {
                *error = "glTF index accessor must be scalar";
            }
            return false;
        }

        for (int i = 0; i + 2 < indices.count; i += 3) {
            int a = static_cast<int>(std::llround(indices.values[static_cast<std::size_t>(i)]));
            int b = static_cast<int>(std::llround(indices.values[static_cast<std::size_t>(i + 1)]));
            int c = static_cast<int>(std::llround(indices.values[static_cast<std::size_t>(i + 2)]));
            if (a < 0 || b < 0 || c < 0 || a >= positions.count || b >= positions.count || c >= positions.count) {
                if (error != nullptr) {
                    *error = "glTF primitive index out of range";
                }
                return false;
            }
            if (mirroredTransform) {
                std::swap(b, c);
            }
            model.faces.push_back({ { baseVertex + a, baseVertex + b, baseVertex + c }, materialIndex });
            model.faceColors.push_back(color);
        }
        return true;
    }

    for (int i = 0; i + 2 < positions.count; i += 3) {
        int a = i;
        int b = i + 1;
        int c = i + 2;
        if (mirroredTransform) {
            std::swap(b, c);
        }
        model.faces.push_back({ { baseVertex + a, baseVertex + b, baseVertex + c }, materialIndex });
        model.faceColors.push_back(color);
    }
    return true;
}

inline bool buildModelFromGltf(
    const JsonValue& gltf,
    const std::vector<std::vector<std::uint8_t>>& buffers,
    Model& model,
    std::string* error)
{
    const JsonValue* nodes = objectMember(gltf, "nodes");
    const JsonValue* meshes = objectMember(gltf, "meshes");
    if (nodes == nullptr || meshes == nullptr || nodes->type != JsonValue::Type::Array || meshes->type != JsonValue::Type::Array) {
        if (error != nullptr) {
            *error = "glTF is missing nodes or meshes";
        }
        return false;
    }

    std::vector<int> roots;
    std::vector<bool> hasParent(nodes->arrayValue.size(), false);
    for (const JsonValue& node : nodes->arrayValue) {
        const JsonValue* children = objectMember(node, "children");
        if (children == nullptr || children->type != JsonValue::Type::Array) {
            continue;
        }
        for (const JsonValue& child : children->arrayValue) {
            const int childIndex = asInt(&child, -1);
            if (childIndex >= 0 && static_cast<std::size_t>(childIndex) < hasParent.size()) {
                hasParent[static_cast<std::size_t>(childIndex)] = true;
            }
        }
    }

    const JsonValue* scenes = objectMember(gltf, "scenes");
    const int sceneIndex = asInt(objectMember(gltf, "scene"), 0);
    const JsonValue* scene = scenes != nullptr && scenes->type == JsonValue::Type::Array && sceneIndex >= 0
        ? arrayElement(*scenes, static_cast<std::size_t>(sceneIndex))
        : nullptr;
    const JsonValue* sceneNodes = scene != nullptr ? objectMember(*scene, "nodes") : nullptr;
    if (sceneNodes != nullptr && sceneNodes->type == JsonValue::Type::Array) {
        for (const JsonValue& rootNode : sceneNodes->arrayValue) {
            const int rootIndex = asInt(&rootNode, -1);
            if (rootIndex >= 0 && static_cast<std::size_t>(rootIndex) < nodes->arrayValue.size()) {
                roots.push_back(rootIndex);
            }
        }
    }
    if (roots.empty()) {
        for (std::size_t i = 0; i < hasParent.size(); ++i) {
            if (!hasParent[i]) {
                roots.push_back(static_cast<int>(i));
            }
        }
    }
    if (roots.empty()) {
        for (std::size_t i = 0; i < nodes->arrayValue.size(); ++i) {
            roots.push_back(static_cast<int>(i));
        }
    }

    std::function<bool(int, const Mat4&)> walkNode = [&](const int nodeIndex, const Mat4& parentMatrix) -> bool {
        const JsonValue* node = arrayElement(*nodes, static_cast<std::size_t>(nodeIndex));
        if (node == nullptr || node->type != JsonValue::Type::Object) {
            if (error != nullptr) {
                *error = "invalid glTF node index";
            }
            return false;
        }

        const Mat4 worldMatrix = multiply(parentMatrix, nodeMatrix(*node));
        const int meshIndex = asInt(objectMember(*node, "mesh"), -1);
        if (meshIndex >= 0) {
            const JsonValue* mesh = arrayElement(*meshes, static_cast<std::size_t>(meshIndex));
            const JsonValue* primitives = mesh != nullptr ? objectMember(*mesh, "primitives") : nullptr;
            if (primitives == nullptr || primitives->type != JsonValue::Type::Array) {
                if (error != nullptr) {
                    *error = "glTF mesh is missing primitives";
                }
                return false;
            }
            for (const JsonValue& primitive : primitives->arrayValue) {
                if (!appendPrimitiveToModel(gltf, buffers, primitive, worldMatrix, model, error)) {
                    return false;
                }
            }
        }

        const JsonValue* children = objectMember(*node, "children");
        if (children != nullptr && children->type == JsonValue::Type::Array) {
            for (const JsonValue& child : children->arrayValue) {
                const int childIndex = asInt(&child, -1);
                if (childIndex < 0) {
                    continue;
                }
                if (!walkNode(childIndex, worldMatrix)) {
                    return false;
                }
            }
        }
        return true;
    };

    const Mat4 identity {};
    for (const int rootIndex : roots) {
        if (!walkNode(rootIndex, identity)) {
            return false;
        }
    }

    if (model.faces.empty()) {
        if (error != nullptr) {
            *error = "no drawable triangles found in glTF";
        }
        return false;
    }

    return true;
}

inline bool decodeGltfDocument(
    const JsonValue& gltf,
    const std::filesystem::path& baseDirectory,
    const std::vector<std::uint8_t>& primaryBin,
    Model& model,
    std::string* error)
{
    if (!rejectUnsupportedExtensions(gltf, error)) {
        return false;
    }

    const JsonValue* buffersArray = objectMember(gltf, "buffers");
    if (buffersArray == nullptr || buffersArray->type != JsonValue::Type::Array || buffersArray->arrayValue.empty()) {
        if (error != nullptr) {
            *error = "glTF has no buffers";
        }
        return false;
    }

    std::vector<std::vector<std::uint8_t>> buffers;
    buffers.reserve(buffersArray->arrayValue.size());
    for (const JsonValue& bufferDef : buffersArray->arrayValue) {
        const JsonValue* uriValue = objectMember(bufferDef, "uri");
        std::vector<std::uint8_t> bytes;
        if (uriValue != nullptr && uriValue->type == JsonValue::Type::String && !uriValue->stringValue.empty()) {
            if (uriValue->stringValue.rfind("data:", 0) == 0) {
                bytes = decodeDataUri(uriValue->stringValue, error);
            } else {
                const auto resolvedPath = resolveRelativePath(baseDirectory, uriValue->stringValue, error);
                if (!resolvedPath) {
                    return false;
                }
                bytes = readFileBytes(*resolvedPath);
            }
        } else if (!primaryBin.empty()) {
            bytes = primaryBin;
        }

        if (bytes.empty()) {
            if (error != nullptr) {
                *error = "glTF buffer payload is missing";
            }
            return false;
        }

        const int byteLength = asInt(objectMember(bufferDef, "byteLength"), 0);
        if (byteLength > 0 && static_cast<std::size_t>(byteLength) > bytes.size()) {
            if (error != nullptr) {
                *error = "glTF buffer is shorter than declared byteLength";
            }
            return false;
        }
        buffers.push_back(std::move(bytes));
    }

    model.materials = buildMaterials(gltf);

    const JsonValue* images = objectMember(gltf, "images");
    if (images != nullptr && images->type == JsonValue::Type::Array) {
        model.images.resize(images->arrayValue.size());
        for (std::size_t index = 0; index < images->arrayValue.size(); ++index) {
            if (!decodeImageRecord(gltf, images->arrayValue[index], buffers, baseDirectory, model.images[index], error)) {
                return false;
            }
        }
    }

    for (const Material& material : model.materials) {
        if (material.baseColorTexture.valid() &&
            static_cast<std::size_t>(material.baseColorTexture.imageIndex) < model.images.size()) {
            model.hasTextureImages = true;
            break;
        }
    }

    return buildModelFromGltf(gltf, buffers, model, error);
}

}  // namespace GltfDetail

inline std::optional<Model> loadGltf(const std::filesystem::path& path, std::string* error)
{
    const std::vector<std::uint8_t> bytes = GltfDetail::readFileBytes(path);
    if (bytes.empty()) {
        if (error != nullptr) {
            *error = "failed to read glTF/GLB file";
        }
        return std::nullopt;
    }

    std::string jsonText;
    std::vector<std::uint8_t> primaryBin;
    std::string loweredExtension = path.has_extension() ? path.extension().string() : std::string {};
    std::transform(loweredExtension.begin(), loweredExtension.end(), loweredExtension.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    if (loweredExtension == ".glb" || (bytes.size() >= 4 && GltfDetail::readU32LE(bytes, 0) == 0x46546C67u)) {
        if (!GltfDetail::decodeGlbChunks(bytes, jsonText, primaryBin, error)) {
            return std::nullopt;
        }
    } else {
        jsonText.assign(bytes.begin(), bytes.end());
    }

    GltfDetail::JsonValue gltf;
    GltfDetail::JsonParser parser(jsonText);
    if (!parser.parse(gltf, error)) {
        return std::nullopt;
    }

    Model model;
    if (!GltfDetail::decodeGltfDocument(gltf, path.parent_path(), primaryBin, model, error)) {
        return std::nullopt;
    }
    model.assetKey = path.generic_string();
    return model;
}

}  // namespace NativeGame
