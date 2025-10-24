#include "NinjamProtocol.h"

#include "Sha1.h"

#include <cstring>

namespace NinjamProtocol {

// ---------------------------------------------------------------------------
// Framing
// ---------------------------------------------------------------------------

void writeFrameHeader(juce::uint8 out[kHeaderSize], juce::uint8 type,
                      juce::uint32 length) {
  out[0] = type;
  const juce::uint32 le = juce::ByteOrder::swapIfBigEndian(length);
  memcpy(out + 1, &le, 4);
}

bool readFrameHeader(const void *fiveBytes, FrameHeader &out) {
  const auto *b = static_cast<const juce::uint8 *>(fiveBytes);
  juce::uint32 le;
  memcpy(&le, b + 1, 4);
  const juce::uint32 len = juce::ByteOrder::swapIfBigEndian(le);
  if (len > kMaxPayload)
    return false;
  out.type = b[0];
  out.length = len;
  return true;
}

// ---------------------------------------------------------------------------
// Reader
// ---------------------------------------------------------------------------

Reader::Reader(const void *data, size_t size) noexcept
    : p(static_cast<const juce::uint8 *>(data)) {
  if (p == nullptr)
    size = 0;
  end = p + size;
}

bool Reader::need(size_t n) noexcept {
  if (failed || remaining() < n) {
    failed = true;
    return false;
  }
  return true;
}

bool Reader::u8(juce::uint8 &out) noexcept {
  if (!need(1))
    return false;
  out = *p++;
  return true;
}

bool Reader::i8(juce::int8 &out) noexcept {
  juce::uint8 v;
  if (!u8(v))
    return false;
  out = static_cast<juce::int8>(v);
  return true;
}

bool Reader::u16le(juce::uint16 &out) noexcept {
  if (!need(2))
    return false;
  out = (juce::uint16)((juce::uint16)p[0] | ((juce::uint16)p[1] << 8));
  p += 2;
  return true;
}

bool Reader::i16le(juce::int16 &out) noexcept {
  juce::uint16 v;
  if (!u16le(v))
    return false;
  // Explicit two's-complement conversion: casting an out-of-range unsigned to
  // a signed type is implementation-defined before C++20.
  out = (v & 0x8000u) ? (juce::int16)((int)v - 65536) : (juce::int16)v;
  return true;
}

bool Reader::u32le(juce::uint32 &out) noexcept {
  if (!need(4))
    return false;
  out = (juce::uint32)p[0] | ((juce::uint32)p[1] << 8) |
        ((juce::uint32)p[2] << 16) | ((juce::uint32)p[3] << 24);
  p += 4;
  return true;
}

bool Reader::bytes(void *dest, size_t n) noexcept {
  if (!need(n))
    return false;
  memcpy(dest, p, n);
  p += n;
  return true;
}

bool Reader::skip(size_t n) noexcept {
  if (!need(n))
    return false;
  p += n;
  return true;
}

bool Reader::cstr(juce::String &out) noexcept {
  if (failed)
    return false;
  const juce::uint8 *nul = p;
  while (nul < end && *nul != 0)
    ++nul;
  if (nul >= end) {
    // No terminator before the end of the payload.
    failed = true;
    return false;
  }
  out = juce::String::fromUTF8(reinterpret_cast<const char *>(p),
                               (int)(nul - p));
  p = nul + 1;
  return true;
}

// ---------------------------------------------------------------------------
// Parsers
// ---------------------------------------------------------------------------

juce::String guidToHex(const juce::uint8 guid[16]) {
  juce::String s;
  s.preallocateBytes(33);
  for (int i = 0; i < 16; ++i)
    s += juce::String::toHexString((int)guid[i]).paddedLeft('0', 2);
  return s;
}

bool IntervalBegin::isOggAudio() const {
  return fourcc[0] == 'O' && fourcc[1] == 'G' && fourcc[2] == 'G' &&
         fourcc[3] == 'v';
}

bool parseAuthChallenge(const juce::MemoryBlock &payload, AuthChallenge &out) {
  Reader r(payload.getData(), payload.getSize());
  return r.bytes(out.challenge, 8);
}

bool parseAuthReply(const juce::MemoryBlock &payload, AuthReply &out) {
  Reader r(payload.getData(), payload.getSize());
  juce::uint8 flag;
  if (!r.u8(flag))
    return false;
  out.granted = (flag == 1);
  return true;
}

bool parseServerConfig(const juce::MemoryBlock &payload, ServerConfig &out) {
  Reader r(payload.getData(), payload.getSize());
  juce::uint16 bpm, bpi;
  if (!r.u16le(bpm) || !r.u16le(bpi))
    return false;
  out.bpm = bpm;
  out.bpi = bpi;
  return true;
}

bool parseUserInfo(const juce::MemoryBlock &payload,
                   std::vector<UserInfoEntry> &out) {
  Reader r(payload.getData(), payload.getSize());
  while (!r.atEnd()) {
    UserInfoEntry e;
    juce::uint8 active, chIdx;
    juce::int16 volume;
    juce::int8 pan;
    // The fixed part of a record is six bytes, not four.
    if (!r.u8(active) || !r.u8(chIdx) || !r.i16le(volume) || !r.i8(pan) ||
        !r.u8(e.flags))
      return false;
    if (!r.cstr(e.username) || !r.cstr(e.channelName))
      return false;

    e.active = (active != 0);
    e.channelIndex = chIdx;
    e.volume = volume;
    e.pan = pan;
    out.push_back(std::move(e));
  }
  return true;
}

bool parseIntervalBegin(const juce::MemoryBlock &payload, IntervalBegin &out) {
  out = IntervalBegin{}; // never leave stale fields when reusing the struct
  Reader r(payload.getData(), payload.getSize());
  if (!r.bytes(out.guid, 16) || !r.u32le(out.estimatedSize) ||
      !r.bytes(out.fourcc, 4))
    return false;

  juce::uint8 chIdx;
  if (!r.u8(chIdx))
    return false;
  out.channelIndex = chIdx;
  out.guidHex = guidToHex(out.guid);

  // The 0x83 upload form stops here; the 0x04 download form adds a username.
  if (!r.atEnd() && !r.cstr(out.username))
    return false;
  return true;
}

bool parseIntervalWrite(const juce::MemoryBlock &payload, IntervalWrite &out) {
  out = IntervalWrite{};
  Reader r(payload.getData(), payload.getSize());
  juce::uint8 flags;
  if (!r.bytes(out.guid, 16) || !r.u8(flags))
    return false;
  out.guidHex = guidToHex(out.guid);
  out.isFinal = (flags & 1) != 0;
  out.audioSize = (int)r.remaining();
  out.audioData = out.audioSize > 0 ? r.rest() : nullptr;
  return true;
}

bool parseChat(const juce::MemoryBlock &payload, Chat &out) {
  out = Chat{}; // trailing fields are optional, so they must start empty
  Reader r(payload.getData(), payload.getSize());
  if (!r.cstr(out.type))
    return false;

  // Trailing fields are optional: a sender may simply stop early. Only a
  // present-but-unterminated field is an error.
  juce::String *fields[4] = {&out.p1, &out.p2, &out.p3, &out.p4};
  for (auto *f : fields) {
    if (r.atEnd())
      break;
    if (!r.cstr(*f))
      return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// Builders
// ---------------------------------------------------------------------------

void computeAuthHash(const juce::String &username, const juce::String &password,
                     const juce::uint8 challenge[8], juce::uint8 out[20]) {
  Sha1 inner;
  inner.add(username.toRawUTF8(), username.getNumBytesAsUTF8());
  inner.add(":", 1);
  inner.add(password.toRawUTF8(), password.getNumBytesAsUTF8());
  juce::uint8 innerDigest[20];
  inner.result(innerDigest);

  Sha1 outer;
  outer.add(innerDigest, 20);
  outer.add(challenge, 8);
  outer.result(out);
}

juce::MemoryBlock buildAuthUser(const juce::uint8 hash[20],
                                const juce::String &username, juce::uint32 caps,
                                juce::uint32 version) {
  juce::MemoryBlock b;
  b.append(hash, 20);
  b.append(username.toRawUTF8(), (size_t)username.getNumBytesAsUTF8() + 1);
  const juce::uint32 leCaps = juce::ByteOrder::swapIfBigEndian(caps);
  b.append(&leCaps, 4);
  const juce::uint32 leVer = juce::ByteOrder::swapIfBigEndian(version);
  b.append(&leVer, 4);
  return b;
}

juce::MemoryBlock
buildUsermask(const std::vector<std::pair<juce::String, juce::uint32>> &masks) {
  juce::MemoryBlock b;
  for (const auto &[name, mask] : masks) {
    b.append(name.toRawUTF8(), (size_t)name.getNumBytesAsUTF8() + 1);
    const juce::uint32 le = juce::ByteOrder::swapIfBigEndian(mask);
    b.append(&le, 4);
  }
  return b;
}

juce::MemoryBlock buildChannelInfo(const juce::StringArray &names) {
  juce::MemoryBlock b;
  // 2-byte LE mpisize: 4 bytes of per-channel metadata follow each name. The
  // server reads exactly this many bytes after every name, so a wrong value
  // desynchronises its parser for all subsequent channels.
  const juce::uint8 mpisize[2] = {4, 0};
  b.append(mpisize, 2);
  for (const auto &name : names) {
    b.append(name.toRawUTF8(), (size_t)name.getNumBytesAsUTF8() + 1);
    const juce::uint8 meta[4] = {0, 0, 0, 0}; // volume LE (0 dB), pan, flags
    b.append(meta, 4);
  }
  return b;
}

juce::MemoryBlock buildIntervalBegin(const juce::uint8 guid[16],
                                     juce::uint32 estimatedSize,
                                     const char fourcc[4], int channelIndex,
                                     const juce::String &username) {
  juce::MemoryBlock b;
  b.append(guid, 16);
  const juce::uint32 leSize = juce::ByteOrder::swapIfBigEndian(estimatedSize);
  b.append(&leSize, 4);
  b.append(fourcc, 4);
  const juce::uint8 chIdx = (juce::uint8)channelIndex;
  b.append(&chIdx, 1);
  if (username.isNotEmpty())
    b.append(username.toRawUTF8(), (size_t)username.getNumBytesAsUTF8() + 1);
  return b;
}

juce::MemoryBlock buildIntervalWrite(const juce::uint8 guid[16], bool isFinal,
                                     const void *audio, int audioSize) {
  juce::MemoryBlock b;
  b.append(guid, 16);
  const juce::uint8 flags = isFinal ? 1 : 0;
  b.append(&flags, 1);
  if (audio != nullptr && audioSize > 0)
    b.append(audio, (size_t)audioSize);
  return b;
}

juce::MemoryBlock buildChat(const juce::String &type, const juce::String &p1,
                            const juce::String &p2, const juce::String &p3,
                            const juce::String &p4) {
  juce::MemoryBlock b;
  const juce::String *fields[5] = {&type, &p1, &p2, &p3, &p4};
  for (auto *f : fields)
    b.append(f->toRawUTF8(), (size_t)f->getNumBytesAsUTF8() + 1);
  return b;
}

} // namespace NinjamProtocol
