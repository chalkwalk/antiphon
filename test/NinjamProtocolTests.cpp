#include <JuceHeader.h>

#include "NinjamProtocol.h"

#include <vector>

namespace {

using namespace NinjamProtocol;

juce::MemoryBlock mb(std::initializer_list<int> bytes) {
  juce::MemoryBlock b;
  for (int v : bytes) {
    const juce::uint8 x = (juce::uint8)v;
    b.append(&x, 1);
  }
  return b;
}

juce::String hex(const juce::uint8 *d, int n) {
  juce::String s;
  for (int i = 0; i < n; ++i)
    s += juce::String::toHexString((int)d[i]).paddedLeft('0', 2);
  return s;
}

class NinjamProtocolTests : public juce::UnitTest {
public:
  NinjamProtocolTests() : juce::UnitTest("NinjamProtocol", "NinjamProtocol") {}

  // Feeds every strict prefix of a valid payload to a parser and requires that
  // none of them is accepted as complete without also being safe. Run this
  // binary under ASan to turn any over-read into a hard failure.
  template <typename Fn>
  void truncationSweep(const juce::MemoryBlock &valid, Fn &&parse,
                       const juce::String &what) {
    for (size_t n = 0; n < valid.getSize(); ++n) {
      juce::MemoryBlock prefix(valid.getData(), n);
      parse(prefix); // must not read out of bounds, must not crash
    }
    expect(true, what + " survived truncation sweep");
  }

  void runTest() override {
    runFramingTests();
    runReaderTests();
    runParserTests();
    runTruncationTests();
    runBuilderTests();
    runAuthTests();
  }

  void runFramingTests() {
    beginTest("frame header round-trip, little-endian length");
    {
      juce::uint8 h[kHeaderSize];
      writeFrameHeader(h, 0xC0, 0x00030201);
      expectEquals((int)h[0], 0xC0);
      expectEquals((int)h[1], 0x01);
      expectEquals((int)h[2], 0x02);
      expectEquals((int)h[3], 0x03);
      expectEquals((int)h[4], 0x00);

      FrameHeader out;
      expect(readFrameHeader(h, out));
      expectEquals((int)out.type, 0xC0);
      expectEquals((int)out.length, 0x00030201);

      // Zero-length payloads (KEEP_ALIVE) round-trip too.
      writeFrameHeader(h, 0xFD, 0);
      expect(readFrameHeader(h, out));
      expectEquals((int)out.type, 0xFD);
      expectEquals((int)out.length, 0);
    }

    beginTest("frame header rejects oversized length");
    {
      juce::uint8 h[kHeaderSize];
      writeFrameHeader(h, 0x05, kMaxPayload + 1);
      FrameHeader out;
      expect(!readFrameHeader(h, out));

      writeFrameHeader(h, 0x05, kMaxPayload);
      expect(readFrameHeader(h, out));
    }
  }

  void runReaderTests() {
    beginTest("Reader refuses to read past the end");
    {
      const juce::uint8 data[3] = {1, 2, 3};
      Reader r(data, 3);
      juce::uint32 v32;
      expect(!r.u32le(v32), "read 4 bytes from a 3-byte buffer");
      expect(!r.ok(), "cursor should latch failed");

      Reader r2(data, 3);
      juce::uint8 a, b, c, d;
      expect(r2.u8(a) && r2.u8(b) && r2.u8(c));
      expect(!r2.u8(d));
    }

    beginTest("Reader::cstr requires a terminator inside the payload");
    {
      const char unterminated[4] = {'a', 'b', 'c', 'd'};
      Reader r(unterminated, 4);
      juce::String s;
      expect(!r.cstr(s), "accepted a string with no NUL");

      const char terminated[4] = {'a', 'b', 'c', '\0'};
      Reader r2(terminated, 4);
      expect(r2.cstr(s));
      expectEquals(s, juce::String("abc"));
      expect(r2.atEnd());
    }

    beginTest("Reader signed conversions");
    {
      auto p = mb({0xFF, 0xFF, 0x80});
      Reader r(p.getData(), p.getSize());
      juce::int16 v16;
      juce::int8 v8;
      expect(r.i16le(v16));
      expectEquals((int)v16, -1);
      expect(r.i8(v8));
      expectEquals((int)v8, -128);
    }

    beginTest("Reader on empty and null buffers");
    {
      Reader r(nullptr, 0);
      juce::uint8 v;
      expect(!r.u8(v));
      expect(r.atEnd());
    }
  }

  void runParserTests() {
    beginTest("0x02 server config is little-endian");
    {
      // bpm = 120 (0x0078), bpi = 16 (0x0010), both little-endian.
      auto p = mb({0x78, 0x00, 0x10, 0x00});
      ServerConfig cfg;
      expect(parseServerConfig(p, cfg));
      expectEquals(cfg.bpm, 120);
      expectEquals(cfg.bpi, 16);
    }

    beginTest("0x01 auth reply");
    {
      AuthReply r;
      expect(parseAuthReply(mb({1}), r));
      expect(r.granted);
      expect(parseAuthReply(mb({0}), r));
      expect(!r.granted);
      expect(!parseAuthReply(juce::MemoryBlock(), r));
    }

    beginTest("0x03 user info round-trip with signed volume and pan");
    {
      juce::MemoryBlock p;
      const juce::uint8 head[6] = {1, 2, 0xFF, 0xFF, 0x80, 0x00};
      p.append(head, 6); // active, chIdx=2, volume=-1, pan=-128, flags=0
      p.append("alice\0", 6);
      p.append("gtr\0", 4);

      std::vector<UserInfoEntry> entries;
      expect(parseUserInfo(p, entries));
      expectEquals((int)entries.size(), 1);
      expect(entries[0].active);
      expectEquals(entries[0].channelIndex, 2);
      expectEquals(entries[0].volume, -1);
      expectEquals(entries[0].pan, -128);
      expectEquals(entries[0].username, juce::String("alice"));
      expectEquals(entries[0].channelName, juce::String("gtr"));
    }

    beginTest("0x03 rejects a record with only four header bytes left");
    {
      // The fixed part of a record is six bytes. The previous implementation
      // checked for four and then read six, running two bytes past the end.
      juce::MemoryBlock p;
      const juce::uint8 head[4] = {1, 0, 0, 0};
      p.append(head, 4);
      std::vector<UserInfoEntry> entries;
      expect(!parseUserInfo(p, entries), "accepted a 4-byte record");
    }

    beginTest("0x03 rejects an unterminated username");
    {
      juce::MemoryBlock p;
      const juce::uint8 head[6] = {1, 0, 0, 0, 0, 0};
      p.append(head, 6);
      p.append("alice", 5); // no NUL: the old code walked off the heap here
      std::vector<UserInfoEntry> entries;
      expect(!parseUserInfo(p, entries), "accepted an unterminated username");
    }

    beginTest("0x03 keeps entries parsed before a malformed record");
    {
      juce::MemoryBlock p;
      const juce::uint8 head[6] = {1, 0, 0, 0, 0, 0};
      p.append(head, 6);
      p.append("bob\0", 4);
      p.append("ch\0", 3);
      p.append(head, 3); // truncated second record
      std::vector<UserInfoEntry> entries;
      expect(!parseUserInfo(p, entries));
      expectEquals((int)entries.size(), 1);
      expectEquals(entries[0].username, juce::String("bob"));
    }

    beginTest("0x04 download interval begin");
    {
      juce::uint8 guid[16];
      for (int i = 0; i < 16; ++i)
        guid[i] = (juce::uint8)(i * 17);
      const char fourcc[4] = {'O', 'G', 'G', 'v'};
      auto p = buildIntervalBegin(guid, 4096, fourcc, 3, "carol");

      IntervalBegin b;
      expect(parseIntervalBegin(p, b));
      expectEquals((int)b.estimatedSize, 4096);
      expectEquals(b.channelIndex, 3);
      expectEquals(b.username, juce::String("carol"));
      expect(b.isOggAudio());
      expectEquals(b.guidHex, hex(guid, 16));
    }

    beginTest("0x83 upload interval begin is exactly 25 bytes, no username");
    {
      juce::uint8 guid[16] = {};
      const char fourcc[4] = {'O', 'G', 'G', 'v'};
      auto p = buildIntervalBegin(guid, 0, fourcc, 1);
      expectEquals((int)p.getSize(), 25, "servers reject a longer 0x83");

      IntervalBegin b;
      expect(parseIntervalBegin(p, b));
      expectEquals(b.channelIndex, 1);
      expect(b.username.isEmpty());
    }

    beginTest("non-OGGv fourcc is reported as non-audio");
    {
      juce::uint8 guid[16] = {};
      const char jtbv[4] = {'J', 'T', 'B', 'v'}; // Jamtaba video
      auto p = buildIntervalBegin(guid, 0, jtbv, 1, "dave");
      IntervalBegin b;
      expect(parseIntervalBegin(p, b));
      expect(!b.isOggAudio());
    }

    beginTest("0x05 interval write flags and payload view");
    {
      juce::uint8 guid[16] = {};
      guid[0] = 0xAB;
      const juce::uint8 audio[5] = {1, 2, 3, 4, 5};

      auto p = buildIntervalWrite(guid, false, audio, 5);
      IntervalWrite w;
      expect(parseIntervalWrite(p, w));
      expect(!w.isFinal);
      expectEquals(w.audioSize, 5);
      expect(memcmp(w.audioData, audio, 5) == 0);

      auto q = buildIntervalWrite(guid, true, nullptr, 0);
      expectEquals((int)q.getSize(), 17);
      expect(parseIntervalWrite(q, w));
      expect(w.isFinal);
      expectEquals(w.audioSize, 0);
    }

    beginTest("0xC0 chat round-trip and optional trailing fields");
    {
      auto p = buildChat("PRIVMSG", "alice", "hi there");
      Chat c;
      expect(parseChat(p, c));
      expectEquals(c.type, juce::String("PRIVMSG"));
      expectEquals(c.p1, juce::String("alice"));
      expectEquals(c.p2, juce::String("hi there"));
      expect(c.p3.isEmpty());
      expect(c.p4.isEmpty());

      // A sender that stops after two fields is legal.
      juce::MemoryBlock q;
      q.append("MSG\0", 4);
      q.append("bob\0", 4);
      expect(parseChat(q, c));
      expectEquals(c.type, juce::String("MSG"));
      expectEquals(c.p1, juce::String("bob"));
      expect(c.p2.isEmpty());
    }

    beginTest("0xC0 rejects a present-but-unterminated field");
    {
      juce::MemoryBlock p;
      p.append("MSG\0", 4);
      p.append("bob", 3); // started a field, never terminated it
      Chat c;
      expect(!parseChat(p, c));
    }

    beginTest("0x80 round-trips, and the tail is optional");
    {
      juce::uint8 hash[20];
      for (int i = 0; i < 20; ++i)
        hash[i] = (juce::uint8)(i + 7);

      AuthUser a;
      expect(parseAuthUser(buildAuthUser(hash, "alice", 1, 0x00020000), a));
      expectEquals(a.username, juce::String("alice"));
      expectEquals((int)a.caps, 1);
      expectEquals((int)a.version, 0x00020000);
      expect(memcmp(a.hash, hash, 20) == 0);

      // A client that stops after the username is still understood.
      juce::MemoryBlock short_;
      short_.append(hash, 20);
      short_.append("bob\0", 4);
      AuthUser b;
      expect(parseAuthUser(short_, b));
      expectEquals(b.username, juce::String("bob"));
      expectEquals((int)b.caps, 0);
    }

    beginTest("0x81 round-trips, and an empty mask is not an absent one");
    {
      std::vector<UsermaskEntry> m;
      expect(parseUsermask(buildUsermask({{"alice", 0x5u}, {"bob", 0u}}), m));
      expectEquals((int)m.size(), 2);
      expectEquals(m[0].username, juce::String("alice"));
      expectEquals((int)m[0].mask, 5);
      // Subscribed to nothing, but present -- which is how a bot goes deaf.
      expectEquals(m[1].username, juce::String("bob"));
      expectEquals((int)m[1].mask, 0);

      std::vector<UsermaskEntry> none;
      expect(parseUsermask({}, none));
      expectEquals((int)none.size(), 0);
    }

    beginTest("0x82 round-trips and honours mpisize");
    {
      std::vector<ChannelInfoEntry> c;
      expect(parseChannelInfo(buildChannelInfo({"gtr", "vox"}), c));
      expectEquals((int)c.size(), 2);
      expectEquals(c[0].name, juce::String("gtr"));
      expectEquals(c[1].name, juce::String("vox"));

      // A wider metadata block must be skipped, not misread as the next name.
      juce::MemoryBlock wide;
      const juce::uint8 mpisize[2] = {6, 0};
      wide.append(mpisize, 2);
      wide.append("gtr\0", 4);
      const juce::uint8 meta[6] = {0, 0, 0, 0, 0xAA, 0xBB};
      wide.append(meta, 6);
      wide.append("vox\0", 4);
      wide.append(meta, 6);
      std::vector<ChannelInfoEntry> w;
      expect(parseChannelInfo(wide, w));
      expectEquals((int)w.size(), 2);
      expectEquals(w[1].name, juce::String("vox"));
    }
  }

  void runTruncationTests() {
    beginTest("every parser survives every truncation");
    // The parsers must be total. Any over-read here is a heap read past the end
    // of a MemoryBlock that is sized to exactly the payload length and is not
    // NUL-padded.
    juce::uint8 guid[16];
    for (int i = 0; i < 16; ++i)
      guid[i] = (juce::uint8)(i + 1);
    const char fourcc[4] = {'O', 'G', 'G', 'v'};
    const juce::uint8 audio[8] = {1, 2, 3, 4, 5, 6, 7, 8};

    juce::MemoryBlock userInfo;
    const juce::uint8 head[6] = {1, 0, 0x10, 0x00, 0x20, 0x00};
    userInfo.append(head, 6);
    userInfo.append("alice\0", 6);
    userInfo.append("guitar\0", 7);
    userInfo.append(head, 6);
    userInfo.append("bob\0", 4);
    userInfo.append("bass\0", 5);

    truncationSweep(
        mb({1, 2, 3, 4, 5, 6, 7, 8}),
        [](const juce::MemoryBlock &p) {
          AuthChallenge c;
          parseAuthChallenge(p, c);
        },
        "0x00");
    truncationSweep(
        mb({1}),
        [](const juce::MemoryBlock &p) {
          AuthReply r;
          parseAuthReply(p, r);
        },
        "0x01");
    truncationSweep(
        mb({0x78, 0x00, 0x10, 0x00}),
        [](const juce::MemoryBlock &p) {
          ServerConfig c;
          parseServerConfig(p, c);
        },
        "0x02");
    truncationSweep(
        userInfo,
        [](const juce::MemoryBlock &p) {
          std::vector<UserInfoEntry> e;
          parseUserInfo(p, e);
        },
        "0x03");
    truncationSweep(
        buildIntervalBegin(guid, 1234, fourcc, 2, "alice"),
        [](const juce::MemoryBlock &p) {
          IntervalBegin b;
          parseIntervalBegin(p, b);
        },
        "0x04");
    truncationSweep(
        buildIntervalWrite(guid, true, audio, 8),
        [](const juce::MemoryBlock &p) {
          IntervalWrite w;
          parseIntervalWrite(p, w);
        },
        "0x05");
    truncationSweep(
        buildChat("PRIVMSG", "alice", "hello", "x", "y"),
        [](const juce::MemoryBlock &p) {
          Chat c;
          parseChat(p, c);
        },
        "0xC0");

    juce::uint8 authHash[20];
    for (int i = 0; i < 20; ++i)
      authHash[i] = (juce::uint8)(i * 3 + 1);
    truncationSweep(
        buildAuthUser(authHash, "alice"),
        [](const juce::MemoryBlock &p) {
          AuthUser a;
          parseAuthUser(p, a);
        },
        "0x80");
    truncationSweep(
        buildUsermask({{"alice", 0x3u}, {"bob", 0x1u}}),
        [](const juce::MemoryBlock &p) {
          std::vector<UsermaskEntry> m;
          parseUsermask(p, m);
        },
        "0x81");
    truncationSweep(
        buildChannelInfo({"gtr", "vox"}),
        [](const juce::MemoryBlock &p) {
          std::vector<ChannelInfoEntry> c;
          parseChannelInfo(p, c);
        },
        "0x82");

    beginTest("parsers survive random garbage");
    {
      juce::Random rng(1234);
      for (int iter = 0; iter < 2000; ++iter) {
        juce::MemoryBlock p((size_t)rng.nextInt(64), false);
        for (size_t i = 0; i < p.getSize(); ++i)
          p[i] = (char)rng.nextInt(256);

        AuthChallenge ac;
        parseAuthChallenge(p, ac);
        AuthReply ar;
        parseAuthReply(p, ar);
        ServerConfig sc;
        parseServerConfig(p, sc);
        std::vector<UserInfoEntry> ui;
        parseUserInfo(p, ui);
        IntervalBegin ib;
        parseIntervalBegin(p, ib);
        IntervalWrite iw;
        parseIntervalWrite(p, iw);
        Chat ch;
        parseChat(p, ch);
      }
      expect(true);
    }
  }

  void runBuilderTests() {
    beginTest("0x80 auth packet layout");
    {
      juce::uint8 hash[20];
      for (int i = 0; i < 20; ++i)
        hash[i] = (juce::uint8)i;
      auto p = buildAuthUser(hash, "tester");
      expectEquals((int)p.getSize(), 20 + 7 + 4 + 4);

      const auto *b = static_cast<const juce::uint8 *>(p.getData());
      expect(memcmp(b, hash, 20) == 0);
      expect(memcmp(b + 20, "tester\0", 7) == 0);
      // caps = 1 LE, version = 0x00020000 LE
      expectEquals((int)b[27], 1);
      expectEquals((int)b[28], 0);
      expectEquals((int)b[29], 0);
      expectEquals((int)b[30], 0);
      expectEquals((int)b[31], 0);
      expectEquals((int)b[32], 0);
      expectEquals((int)b[33], 0x02);
      expectEquals((int)b[34], 0);
    }

    beginTest("0x81 usermask bitmask layout");
    {
      // Channels 0, 3 and 5 enabled -> 0b101001 = 0x29.
      std::vector<std::pair<juce::String, juce::uint32>> masks{{"alice", 0x29}};
      auto p = buildUsermask(masks);
      expectEquals((int)p.getSize(), 6 + 4);
      const auto *b = static_cast<const juce::uint8 *>(p.getData());
      expect(memcmp(b, "alice\0", 6) == 0);
      expectEquals((int)b[6], 0x29);
      expectEquals((int)b[7], 0);
      expectEquals((int)b[8], 0);
      expectEquals((int)b[9], 0);
    }

    beginTest("0x82 channel info layout with mpisize");
    {
      auto p = buildChannelInfo({"gtr", "bass"});
      // 2 (mpisize) + 4 ("gtr\0") + 4 (meta) + 5 ("bass\0") + 4 (meta)
      expectEquals((int)p.getSize(), 19);
      const auto *b = static_cast<const juce::uint8 *>(p.getData());
      expectEquals((int)b[0], 4);
      expectEquals((int)b[1], 0);
      expect(memcmp(b + 2, "gtr\0", 4) == 0);
      for (int i = 6; i < 10; ++i)
        expectEquals((int)b[i], 0);
      expect(memcmp(b + 10, "bass\0", 5) == 0);
    }

    beginTest("0x82 with no channels is just the mpisize header");
    {
      expectEquals((int)buildChannelInfo({}).getSize(), 2);
    }
  }

  void runAuthTests() {
    beginTest("auth hash matches an independent SHA1 implementation");
    // Goldens computed with Python hashlib, not with our own Sha1 class:
    //   sha1(sha1(user + ":" + pass) + challenge)
    juce::uint8 challenge[8];
    for (int i = 0; i < 8; ++i)
      challenge[i] = (juce::uint8)i;

    juce::uint8 out[20];

    computeAuthHash("tester", "", challenge, out);
    expectEquals(hex(out, 20),
                 juce::String("0471f0ad9885d825ce678e75cf23668c994068f8"));

    computeAuthHash("alice", "secret", challenge, out);
    expectEquals(hex(out, 20),
                 juce::String("7f5c31b13ebe89c36c8e3b5ee59720e238bb6422"));

    // The anonymous login form used by the server browser.
    computeAuthHash("anonymous:bob", "", challenge, out);
    expectEquals(hex(out, 20),
                 juce::String("81d28bdad1230452f6ae94f940c9f9ce94b4d0b4"));
  }
};

static NinjamProtocolTests ninjamProtocolTests;

} // namespace
