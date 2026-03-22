#pragma once

#include "NativeGame/Hash.hpp"
#include "NativeGame/NetProtocol.hpp"

#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace NativeGame {

namespace BlobSyncDetail {

constexpr std::string_view kBase64Chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

inline int decodeBase64Char(char ch)
{
    if (ch >= 'A' && ch <= 'Z') {
        return ch - 'A';
    }
    if (ch >= 'a' && ch <= 'z') {
        return ch - 'a' + 26;
    }
    if (ch >= '0' && ch <= '9') {
        return ch - '0' + 52;
    }
    if (ch == '+') {
        return 62;
    }
    if (ch == '/') {
        return 63;
    }
    return -1;
}

inline std::string encodeBase64(const std::string& raw)
{
    std::string encoded;
    encoded.reserve(((raw.size() + 2u) / 3u) * 4u);
    for (std::size_t index = 0; index < raw.size(); index += 3u) {
        const unsigned char b1 = static_cast<unsigned char>(raw[index]);
        const unsigned char b2 = (index + 1u < raw.size()) ? static_cast<unsigned char>(raw[index + 1u]) : 0u;
        const unsigned char b3 = (index + 2u < raw.size()) ? static_cast<unsigned char>(raw[index + 2u]) : 0u;
        const std::uint32_t block =
            (static_cast<std::uint32_t>(b1) << 16u) |
            (static_cast<std::uint32_t>(b2) << 8u) |
            static_cast<std::uint32_t>(b3);
        encoded.push_back(kBase64Chars[(block >> 18u) & 63u]);
        encoded.push_back(kBase64Chars[(block >> 12u) & 63u]);
        encoded.push_back(index + 1u < raw.size() ? kBase64Chars[(block >> 6u) & 63u] : '=');
        encoded.push_back(index + 2u < raw.size() ? kBase64Chars[block & 63u] : '=');
    }
    return encoded;
}

inline std::optional<std::string> decodeBase64(const std::string& encoded)
{
    std::string compact;
    compact.reserve(encoded.size());
    for (const unsigned char ch : encoded) {
        if (!std::isspace(ch)) {
            compact.push_back(static_cast<char>(ch));
        }
    }
    if ((compact.size() % 4u) != 0u) {
        return std::nullopt;
    }

    std::string raw;
    raw.reserve((compact.size() / 4u) * 3u);
    for (std::size_t index = 0; index < compact.size(); index += 4u) {
        const int c1 = decodeBase64Char(compact[index]);
        const int c2 = decodeBase64Char(compact[index + 1u]);
        const int c3 = compact[index + 2u] == '=' ? -2 : decodeBase64Char(compact[index + 2u]);
        const int c4 = compact[index + 3u] == '=' ? -2 : decodeBase64Char(compact[index + 3u]);
        if (c1 < 0 || c2 < 0 || c3 == -1 || c4 == -1) {
            return std::nullopt;
        }

        const std::uint32_t block =
            (static_cast<std::uint32_t>(c1) << 18u) |
            (static_cast<std::uint32_t>(c2) << 12u) |
            (static_cast<std::uint32_t>(std::max(c3, 0)) << 6u) |
            static_cast<std::uint32_t>(std::max(c4, 0));
        raw.push_back(static_cast<char>((block >> 16u) & 0xFFu));
        if (c3 != -2) {
            raw.push_back(static_cast<char>((block >> 8u) & 0xFFu));
        }
        if (c4 != -2) {
            raw.push_back(static_cast<char>(block & 0xFFu));
        }
    }
    return raw;
}

}  // namespace BlobSyncDetail

struct BlobOutgoingTransfer {
    std::string kind;
    std::string hash;
    std::string raw;
    int rawBytes = 0;
    int encodedBytes = 0;
    int chunkSize = 0;
    int chunkCount = 0;
    BlobMetaPacket meta {};
    std::vector<std::string> chunks;
};

struct BlobIncomingTransfer {
    BlobMetaPacket meta {};
    int chunkCount = 0;
    int chunkSize = 0;
    int receivedCount = 0;
    std::map<int, std::string> chunks;
};

struct BlobSyncState {
    std::unordered_map<std::string, BlobOutgoingTransfer> outgoing;
    std::unordered_map<std::string, BlobIncomingTransfer> incoming;
};

inline std::string blobTransferKey(std::string_view kind, std::string_view hash)
{
    return std::string(kind) + "|" + std::string(hash);
}

inline BlobSyncState createBlobSyncState()
{
    return {};
}

inline BlobOutgoingTransfer prepareOutgoingBlobTransfer(
    BlobSyncState& state,
    std::string_view kind,
    std::string_view hash,
    const std::string& rawBytes,
    const BlobMetaPacket& baseMeta = {},
    int chunkSize = 720)
{
    BlobOutgoingTransfer transfer;
    transfer.kind = std::string(kind);
    transfer.hash = std::string(hash);
    transfer.raw = rawBytes;
    transfer.rawBytes = static_cast<int>(rawBytes.size());
    transfer.chunkSize = std::max(64, chunkSize);
    transfer.meta = baseMeta;
    transfer.meta.kind = transfer.kind;
    transfer.meta.hash = transfer.hash.empty() ? sha256Hex(rawBytes) : transfer.hash;
    transfer.hash = transfer.meta.hash;
    transfer.meta.rawBytes = transfer.rawBytes;
    transfer.meta.chunkSize = transfer.chunkSize;

    const std::string encoded = BlobSyncDetail::encodeBase64(rawBytes);
    transfer.encodedBytes = static_cast<int>(encoded.size());
    transfer.meta.encodedBytes = transfer.encodedBytes;
    for (std::size_t offset = 0; offset < encoded.size(); offset += static_cast<std::size_t>(transfer.chunkSize)) {
        transfer.chunks.push_back(encoded.substr(offset, static_cast<std::size_t>(transfer.chunkSize)));
    }
    transfer.chunkCount = static_cast<int>(transfer.chunks.size());
    transfer.meta.chunkCount = transfer.chunkCount;

    state.outgoing[blobTransferKey(kind, hash)] = transfer;
    return transfer;
}

inline std::optional<BlobIncomingTransfer> acceptIncomingBlobMeta(BlobSyncState& state, const BlobMetaPacket& meta)
{
    if (meta.kind.empty() || meta.hash.empty() || meta.chunkCount <= 0 || meta.chunkSize <= 0) {
        return std::nullopt;
    }

    BlobIncomingTransfer transfer;
    transfer.meta = meta;
    transfer.chunkCount = meta.chunkCount;
    transfer.chunkSize = meta.chunkSize;
    state.incoming[blobTransferKey(meta.kind, meta.hash)] = transfer;
    return transfer;
}

inline std::optional<std::pair<BlobMetaPacket, std::string>> acceptIncomingBlobChunk(
    BlobSyncState& state,
    const BlobChunkPacket& chunk)
{
    auto it = state.incoming.find(blobTransferKey(chunk.kind, chunk.hash));
    if (it == state.incoming.end()) {
        return std::nullopt;
    }
    BlobIncomingTransfer& transfer = it->second;
    if (chunk.index < 1 || chunk.index > transfer.chunkCount || static_cast<int>(chunk.data.size()) > transfer.chunkSize) {
        return std::nullopt;
    }
    if (transfer.chunks.emplace(chunk.index, chunk.data).second) {
        ++transfer.receivedCount;
    }
    if (transfer.receivedCount < transfer.chunkCount) {
        return std::nullopt;
    }

    std::string encoded;
    for (int index = 1; index <= transfer.chunkCount; ++index) {
        const auto chunkIt = transfer.chunks.find(index);
        if (chunkIt == transfer.chunks.end()) {
            return std::nullopt;
        }
        encoded.append(chunkIt->second);
    }
    std::optional<std::string> decoded = BlobSyncDetail::decodeBase64(encoded);
    if (!decoded.has_value()) {
        return std::nullopt;
    }
    if (transfer.meta.rawBytes > 0 && static_cast<int>(decoded->size()) != transfer.meta.rawBytes) {
        return std::nullopt;
    }
    if (!transfer.meta.hash.empty() && sha256Hex(*decoded) != transfer.meta.hash) {
        return std::nullopt;
    }

    const BlobMetaPacket meta = transfer.meta;
    state.incoming.erase(it);
    return std::make_pair(meta, *decoded);
}

}  // namespace NativeGame
