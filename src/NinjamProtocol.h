#pragma once
#include <JuceHeader.h>
#include <utility>
#include <vector>

// Byte-level Ninjam protocol: framing, message parsing, message building.
//
// Everything here is pure -- no sockets, no threads, no shared state -- so it
// can be exercised directly by tests, including with deliberately malformed
// input. NinjamClient keeps the stateful dispatch; only the wire format lives
// here.
//
// All multi-byte integers in the Ninjam protocol are LITTLE-ENDIAN
// (justinfrankel/ninjam mpb.cpp:192-195 for bpm/bpi, :281-282 for volume).

namespace NinjamProtocol {

enum Msg : juce::uint8 {
  ServerAuthChallenge = 0x00,
  ServerAuthReply = 0x01,
  ServerConfigChange = 0x02,
  ServerUserInfoChange = 0x03,
  DownloadIntervalBegin = 0x04,
  DownloadIntervalWrite = 0x05,
  ClientAuthUser = 0x80,
  ClientSetUsermask = 0x81,
  ClientSetChannelInfo = 0x82,
  UploadIntervalBegin = 0x83,
  UploadIntervalWrite = 0x84,
  ChatMessage = 0xC0,
  KeepAlive = 0xFD
};

// ---------------------------------------------------------------------------
// Framing: 1-byte type + 4-byte little-endian payload length.
// ---------------------------------------------------------------------------

static constexpr int kHeaderSize = 5;
static constexpr juce::uint32 kMaxPayload = 10u * 1024 * 1024;

struct FrameHeader {
  juce::uint8 type = 0;
  juce::uint32 length = 0;
};

void writeFrameHeader(juce::uint8 out[kHeaderSize], juce::uint8 type,
                      juce::uint32 length);

// Rejects lengths above kMaxPayload.
bool readFrameHeader(const void *fiveBytes, FrameHeader &out);

// ---------------------------------------------------------------------------
// Bounds-checked cursor. Every accessor returns false and leaves the output
// untouched if the read would run past the end; once a read fails the cursor
// latches failed so callers may check ok() once at the end instead of after
// every field.
// ---------------------------------------------------------------------------

class Reader {
public:
  Reader(const void *data, size_t size) noexcept;

  bool u8(juce::uint8 &out) noexcept;
  bool i8(juce::int8 &out) noexcept;
  bool u16le(juce::uint16 &out) noexcept;
  bool i16le(juce::int16 &out) noexcept;
  bool u32le(juce::uint32 &out) noexcept;
  bool bytes(void *dest, size_t n) noexcept;
  bool skip(size_t n) noexcept;

  // Reads up to the next NUL. Fails, without advancing, if no NUL appears
  // before the end of the payload. This is what keeps a truncated or hostile
  // record from walking off the end of the buffer.
  bool cstr(juce::String &out) noexcept;

  const void *rest() const noexcept { return p; }
  size_t remaining() const noexcept { return (size_t)(end - p); }
  bool ok() const noexcept { return !failed; }
  bool atEnd() const noexcept { return p >= end; }

private:
  bool need(size_t n) noexcept;

  const juce::uint8 *p;
  const juce::uint8 *end;
  bool failed = false;
};

// ---------------------------------------------------------------------------
// Parsed message forms.
// ---------------------------------------------------------------------------

struct AuthChallenge {
  juce::uint8 challenge[8] = {};
};

struct AuthReply {
  bool granted = false;
  juce::String errorMessage;
  // Maximum local channel index the server will accept. The reference client
  // refuses to transmit on any channel at or above this
  // (justinfrankel/ninjam njclient.cpp:1096, :1476), so a server that
  // omits it gets no audio at all from a stock client. Absent on older
  // servers, in which case it stays 0.
  int maxChannels = 0;
};

struct ServerConfig {
  int bpm = 0;
  int bpi = 0;
};

struct UserInfoEntry {
  bool active = false;
  int channelIndex = 0;
  int volume = 0;
  int pan = 0;
  juce::uint8 flags = 0;
  juce::String username;
  juce::String channelName;
};

struct IntervalBegin {
  juce::uint8 guid[16] = {};
  juce::String guidHex;
  juce::uint32 estimatedSize = 0;
  char fourcc[4] = {};
  int channelIndex = 0;
  juce::String username; // empty for the 0x83 upload form
  bool isOggAudio() const;
};

struct IntervalWrite {
  juce::uint8 guid[16] = {};
  juce::String guidHex;
  bool isFinal = false;
  const void *audioData = nullptr; // view into the caller's payload
  int audioSize = 0;
};

struct Chat {
  juce::String type, p1, p2, p3, p4;
};

// The client-sent messages, which only a server has any reason to read. They
// live here with the rest of the wire format so they get the same bounds
// checking and the same truncation sweep; PracticeServer holds the state.
struct AuthUser {
  juce::uint8 hash[20] = {};
  juce::String username;
  juce::uint32 caps = 0;
  juce::uint32 version = 0;
};

// Which channels of which player this client wants sent to it. A player absent
// from the list has not been subscribed to.
struct UsermaskEntry {
  juce::String username;
  juce::uint32 mask = 0;
};

struct ChannelInfoEntry {
  juce::String name;
  int volume = 0;
  int pan = 0;
  juce::uint8 flags = 0;
};

// Each returns false on malformed input, having read nothing past the payload.
bool parseAuthChallenge(const juce::MemoryBlock &payload, AuthChallenge &out);
bool parseAuthReply(const juce::MemoryBlock &payload, AuthReply &out);
bool parseServerConfig(const juce::MemoryBlock &payload, ServerConfig &out);

// Returns false if any record is malformed. Records successfully parsed before
// the failure are retained in `out`, matching the reference server's forgiving
// treatment of trailing garbage.
bool parseUserInfo(const juce::MemoryBlock &payload,
                   std::vector<UserInfoEntry> &out);

// Handles both DOWNLOAD_INTERVAL_BEGIN (0x04, with username) and
// UPLOAD_INTERVAL_BEGIN (0x83, exactly 25 bytes, no username).
bool parseIntervalBegin(const juce::MemoryBlock &payload, IntervalBegin &out);

// Handles both 0x05 and 0x84 -- the payload layouts are identical.
bool parseIntervalWrite(const juce::MemoryBlock &payload, IntervalWrite &out);

bool parseChat(const juce::MemoryBlock &payload, Chat &out);

bool parseAuthUser(const juce::MemoryBlock &payload, AuthUser &out);

// As with parseUserInfo, entries read before a malformed one are retained.
bool parseUsermask(const juce::MemoryBlock &payload,
                   std::vector<UsermaskEntry> &out);

// The leading 2-byte mpisize gives the per-channel metadata width, which is 4
// in every client seen but is honoured rather than assumed -- a wrong guess
// desynchronises the parse for every channel after the first.
bool parseChannelInfo(const juce::MemoryBlock &payload,
                      std::vector<ChannelInfoEntry> &out);

// ---------------------------------------------------------------------------
// Builders.
// ---------------------------------------------------------------------------

// Ninjam challenge-response: SHA1(SHA1(user + ":" + pass) + challenge[0..8]).
void computeAuthHash(const juce::String &username, const juce::String &password,
                     const juce::uint8 challenge[8], juce::uint8 out[20]);

// Server side, used by the test fixtures. A real server always sends the
// channel cap; omitting it stops a stock client transmitting entirely.
juce::MemoryBlock buildAuthReply(bool granted,
                                 const juce::String &errorMessage = {},
                                 int maxChannels = 32);

juce::MemoryBlock buildAuthChallenge(const juce::uint8 challenge[8],
                                     juce::uint32 caps = 0,
                                     juce::uint32 version = 0x00020000,
                                     const juce::String &licence = {});

juce::MemoryBlock buildServerConfig(int bpm, int bpi);

juce::MemoryBlock buildUserInfo(const std::vector<UserInfoEntry> &entries);

juce::MemoryBlock buildAuthUser(const juce::uint8 hash[20],
                                const juce::String &username,
                                juce::uint32 caps = 1,
                                juce::uint32 version = 0x00020000);

// Channel indices >= 32 are dropped rather than shifted (1u << 32 is UB).
juce::MemoryBlock
buildUsermask(const std::vector<std::pair<juce::String, juce::uint32>> &masks);

juce::MemoryBlock buildChannelInfo(const juce::StringArray &names);

// Pass an empty username for the 0x83 upload form (exactly 25 bytes).
juce::MemoryBlock buildIntervalBegin(const juce::uint8 guid[16],
                                     juce::uint32 estimatedSize,
                                     const char fourcc[4], int channelIndex,
                                     const juce::String &username = {});

juce::MemoryBlock buildIntervalWrite(const juce::uint8 guid[16], bool isFinal,
                                     const void *audio, int audioSize);

juce::MemoryBlock buildChat(const juce::String &type,
                            const juce::String &p1 = {},
                            const juce::String &p2 = {},
                            const juce::String &p3 = {},
                            const juce::String &p4 = {});

juce::String guidToHex(const juce::uint8 guid[16]);

} // namespace NinjamProtocol
