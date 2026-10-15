#pragma once
#include <cstddef>
#include <cstdint>

/* Application-owned buffers only. Decoder/TLS/lwIP/display allocations need
 * separate internal-heap headroom on the no-PSRAM C3. Do not grow these from
 * pre-decode free-heap observations: the MP3 core allocates lazily. */
namespace radio_buffers {
constexpr std::size_t kCompressedBytes=8192;
constexpr std::size_t kPcmBytes=8192;
constexpr std::size_t kDecoderInputBytes=4096;
constexpr std::size_t kDecoderOutputBytes=4608;
// Target playback trial: 256-byte reads ran without observed rebuffer/underrun
// while retaining the same bounded queues, decoder and output path.
constexpr unsigned kDefaultReadBytes=256;
constexpr unsigned kMaxReadBytes=1024;
constexpr std::size_t kStartupBytes=2048;
constexpr int64_t kStartupWaitUs=500000;
constexpr std::size_t kOwnedBufferBytes=kCompressedBytes+kPcmBytes+kDecoderInputBytes+kDecoderOutputBytes;
static_assert(kOwnedBufferBytes<=25*1024,"Preserve receive/decoder heap headroom; remeasure before increasing this budget");
static_assert(kStartupBytes<=kCompressedBytes,"Prefill must fit compressed reserve");
static_assert(kMaxReadBytes==1024&&(kDefaultReadBytes==256||kDefaultReadBytes==1024),"Read buffer and validated sizes must remain consistent");
}
