#pragma once
// Memory Expansion Fabric - framed loopback transport protocol.
//
// Frame wire format (little-endian):
//   magic   : 4 bytes  'M','E','F','W'
//   version : u16      protocol version
//   type    : u16      FrameType
//   len     : u32      payload length (bounded)
//   crc32   : u32      CRC-32 over payload
//   payload : len bytes
//
// Frames are bounded; the decoder rejects malformed, truncated, oversized,
// unknown-type and checksum-failed input. Frame-type validity is validated
// against an explicit set, never a "stale integer maximum".
#include "memory_expansion_fabric/id.hpp"
#include "memory_expansion_fabric/result.hpp"
#include "memory_expansion_fabric/types.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace memory_expansion_fabric {

enum class FrameType : std::uint16_t {
    HELLO = 1,
    HELLO_ACK = 2,
    HEARTBEAT = 3,
    BYE = 4,
    PUBLISH_EVIDENCE = 5,
    PUBLISH_ATTACHMENT = 6,
    SELECT = 7,
    SELECT_RESP = 8,
    RESERVE = 9,
    RESERVE_RESP = 10,
    COMMIT = 11,
    COMMIT_RESP = 12,
    RELEASE = 13,
    RELEASE_RESP = 14,
    INSPECT = 15,
    INSPECT_RESP = 16,
    AUDIT = 17,
    AUDIT_RESP = 18,
    SPAWN_WORKER = 19,
    SPAWN_WORKER_RESP = 20,
    TERMINATE_WORKER = 21,
    TERMINATE_WORKER_RESP = 22,
    SAVE = 23,
    SAVE_RESP = 24,
};

constexpr std::uint16_t kProtocolVersion = 1;
constexpr std::uint32_t kMaxFramePayload = 1024u * 1024u; // 1 MiB

bool isKnownFrameType(std::uint16_t t);

struct Frame {
    FrameType type = FrameType::HELLO;
    std::vector<std::uint8_t> payload;
};

enum class FrameDecode { OK, INCOMPLETE, ERROR };

// Encode a single frame. Returns the exact bytes to send on the wire.
Result<std::vector<std::uint8_t>> encodeFrame(FrameType type,
                                              const std::vector<std::uint8_t>& payload);

// Decode exactly one frame from data[0..len). On OK, out is populated and
// consumed is the number of bytes consumed. On INCOMPLETE, need more bytes.
// On ERROR, err describes the failure.
FrameDecode decodeFrame(const std::uint8_t* data, std::size_t len, Frame& out,
                        std::size_t& consumed, std::string& err);

// --- typed payload codecs (used by the reference deployment) ----------------
Result<std::vector<std::uint8_t>> encodeRegionEvidence(const RegionEvidence& e);
Result<RegionEvidence> decodeRegionEvidence(const std::vector<std::uint8_t>& b);
Result<std::vector<std::uint8_t>> encodeRequirements(const ConsumerRequirements& r);
Result<ConsumerRequirements> decodeRequirements(const std::vector<std::uint8_t>& b);
Result<std::vector<std::uint8_t>> encodeSelection(const SelectionResult& s);
Result<SelectionResult> decodeSelection(const std::vector<std::uint8_t>& b);
Result<std::vector<std::uint8_t>> encodeReservation(const Reservation& r);
Result<Reservation> decodeReservation(const std::vector<std::uint8_t>& b);

// Wrap a string (used for single-string response payloads like inspect/audit).
inline std::vector<std::uint8_t> makeStringPayload(const std::string& s) {
    return std::vector<std::uint8_t>(s.begin(), s.end());
}

} // namespace memory_expansion_fabric
