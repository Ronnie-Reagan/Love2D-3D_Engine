#pragma once

#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#endif

namespace NativeGame {

inline std::string fnv1a32Hex(std::string_view bytes)
{
    std::uint32_t hash = 2166136261u;
    for (unsigned char byte : bytes) {
        hash ^= static_cast<std::uint32_t>(byte);
        hash *= 16777619u;
    }

    std::ostringstream stream;
    stream << "fnv" << std::hex << std::setfill('0') << std::setw(8) << hash;
    return stream.str();
}

inline std::string bytesToHex(const std::vector<std::uint8_t>& bytes)
{
    std::ostringstream stream;
    stream << std::hex << std::setfill('0');
    for (const std::uint8_t byte : bytes) {
        stream << std::setw(2) << static_cast<int>(byte);
    }
    return stream.str();
}

inline std::string sha256Hex(std::string_view bytes)
{
#if defined(_WIN32)
    BCRYPT_ALG_HANDLE algorithmHandle = nullptr;
    BCRYPT_HASH_HANDLE hashHandle = nullptr;
    DWORD objectBytes = 0;
    DWORD dataBytes = 0;
    DWORD hashBytes = 0;

    if (BCryptOpenAlgorithmProvider(&algorithmHandle, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) {
        return fnv1a32Hex(bytes);
    }

    if (BCryptGetProperty(
            algorithmHandle,
            BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&objectBytes),
            sizeof(objectBytes),
            &dataBytes,
            0) != 0 ||
        BCryptGetProperty(
            algorithmHandle,
            BCRYPT_HASH_LENGTH,
            reinterpret_cast<PUCHAR>(&hashBytes),
            sizeof(hashBytes),
            &dataBytes,
            0) != 0) {
        BCryptCloseAlgorithmProvider(algorithmHandle, 0);
        return fnv1a32Hex(bytes);
    }

    std::vector<std::uint8_t> objectBuffer(objectBytes);
    std::vector<std::uint8_t> hashBuffer(hashBytes);
    if (BCryptCreateHash(
            algorithmHandle,
            &hashHandle,
            objectBuffer.data(),
            static_cast<ULONG>(objectBuffer.size()),
            nullptr,
            0,
            0) != 0) {
        BCryptCloseAlgorithmProvider(algorithmHandle, 0);
        return fnv1a32Hex(bytes);
    }

    const PUCHAR data = bytes.empty()
        ? nullptr
        : const_cast<PUCHAR>(reinterpret_cast<const UCHAR*>(bytes.data()));
    const ULONG size = static_cast<ULONG>(bytes.size());
    const bool ok =
        BCryptHashData(hashHandle, data, size, 0) == 0 &&
        BCryptFinishHash(hashHandle, hashBuffer.data(), static_cast<ULONG>(hashBuffer.size()), 0) == 0;

    BCryptDestroyHash(hashHandle);
    BCryptCloseAlgorithmProvider(algorithmHandle, 0);
    return ok ? bytesToHex(hashBuffer) : fnv1a32Hex(bytes);
#else
    return fnv1a32Hex(bytes);
#endif
}

}  // namespace NativeGame
