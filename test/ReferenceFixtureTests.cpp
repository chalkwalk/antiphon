#include <JuceHeader.h>

#include "NinjamProtocol.h"
#include "TestSignal.h"
#include "VorbisCodec.h"

#include <vector>

// Regression tests against bytes the OFFICIAL NINJAM client actually produced.
//
// These are the durable half of the interop work. The harness in
// test/refclient/ proved parity once, against a live reference client; these
// fixtures preserve that evidence so it keeps being checked after the harness
// is deleted. They are fast, hermetic, and need no server, no GPL linkage and
// no timing.
//
// Fixtures were captured by pointing the reference client at our own
// FakeNinjamServer and recording every byte it sent -- see
// test/refclient/README.md. Re-capture with:
//
//   NINJAM_INTEROP=1 NINJAM_CAPTURE_FIXTURES=test/fixtures/reference \
//     ./build/test/NinjamTests_artefacts/NinjamTests Interop
//
// Do not hand-edit them: their whole value is that no code of ours produced
// them.

namespace {

juce::File fixtureDir() {
  // Walk up from the test binary to the repo, then into test/fixtures.
  auto dir = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
  for (int i = 0; i < 6 && dir.exists(); ++i) {
    dir = dir.getParentDirectory();
    auto candidate = dir.getChildFile("test")
                         .getChildFile("fixtures")
                         .getChildFile("reference");
    if (candidate.isDirectory())
      return candidate;
  }
  return {};
}

juce::MemoryBlock loadFixture(const juce::String &name) {
  juce::MemoryBlock mb;
  const auto dir = fixtureDir();
  if (dir.isDirectory())
    dir.getChildFile(name).loadFileAsData(mb);
  return mb;
}

class ReferenceFixtureTests : public juce::UnitTest {
public:
  ReferenceFixtureTests()
      : juce::UnitTest("ReferenceFixtures", "ReferenceFixtures") {}

  void runTest() override {
    const auto dir = fixtureDir();
    if (!dir.isDirectory()) {
      beginTest("reference fixtures");
      logMessage("test/fixtures/reference not found -- skipping.");
      expect(true);
      return;
    }

    testAuthPacket();
    testChannelInfoPacket();
    testUploadBeginPacket();
    testUsermaskPacket();
    testReferenceOggDecodes();
  }

  // -------------------------------------------------------------------
  // 0x80 CLIENT_AUTH_USER, exactly as the reference client sent it.
  // -------------------------------------------------------------------
  void testAuthPacket() {
    beginTest("reference CLIENT_AUTH_USER parses and round-trips");
    auto raw = loadFixture("80_client_auth_user.bin");
    if (raw.getSize() == 0) {
      logMessage("fixture missing -- skipping");
      expect(true);
      return;
    }

    // Layout: 20-byte hash + NUL-terminated username + 4-byte caps + 4-byte
    // version, both little-endian.
    expect(raw.getSize() > 29, "auth packet implausibly short");

    NinjamProtocol::Reader r(raw.getData(), raw.getSize());
    juce::uint8 hash[20];
    juce::String username;
    juce::uint32 caps = 0, version = 0;
    expect(r.bytes(hash, 20));
    expect(r.cstr(username));
    expect(r.u32le(caps));
    expect(r.u32le(version));
    expect(r.ok() && r.atEnd(),
           "auth packet had trailing bytes we do not account for");

    logMessage("reference auth: user '" + username + "', caps " +
               juce::String((int)caps) + ", version 0x" +
               juce::String::toHexString((int)version));

    expect(username.isNotEmpty(), "no username in the reference auth packet");
    expectEquals((int)version, 0x00020000,
                 "protocol version differs from the reference client");

    // Our builder must produce a byte-identical packet from the same inputs.
    auto ours = NinjamProtocol::buildAuthUser(hash, username, caps, version);
    expectEquals((int)ours.getSize(), (int)raw.getSize());
    expect(ours == raw,
           "our CLIENT_AUTH_USER differs byte-for-byte from the reference");
  }

  // -------------------------------------------------------------------
  // 0x82 CLIENT_SET_CHANNEL_INFO -- the mpisize field is the one that
  // desynchronises a server's parser if we get it wrong.
  // -------------------------------------------------------------------
  void testChannelInfoPacket() {
    beginTest("reference CLIENT_SET_CHANNEL_INFO layout matches ours");
    auto raw = loadFixture("82_client_set_channel_info.bin");
    if (raw.getSize() == 0) {
      logMessage("fixture missing -- skipping");
      expect(true);
      return;
    }

    const auto *b = static_cast<const juce::uint8 *>(raw.getData());
    expect(raw.getSize() >= 2, "channel info too short");
    const int mpisize = (int)b[0] | ((int)b[1] << 8);
    logMessage("reference mpisize = " + juce::String(mpisize));
    expectEquals(mpisize, 4,
                 "reference uses a different per-channel metadata size");

    // Read the channel names the reference declared.
    NinjamProtocol::Reader r(raw.getData(), raw.getSize());
    juce::uint16 msz;
    expect(r.u16le(msz));
    juce::StringArray names;
    while (!r.atEnd()) {
      juce::String name;
      if (!r.cstr(name))
        break;
      if (!r.skip((size_t)msz))
        break;
      names.add(name);
    }
    expect(names.size() >= 1, "no channel names in the reference packet");
    logMessage("reference channels: " + names.joinIntoString(", "));

    // Our builder must agree for the same channel list.
    auto ours = NinjamProtocol::buildChannelInfo(names);
    expect(ours == raw,
           "our CLIENT_SET_CHANNEL_INFO differs from the reference for the "
           "same channel names");
  }

  // -------------------------------------------------------------------
  // 0x83 UPLOAD_INTERVAL_BEGIN -- servers reject anything but 25 bytes.
  // -------------------------------------------------------------------
  void testUploadBeginPacket() {
    beginTest("reference UPLOAD_INTERVAL_BEGIN is 25 bytes of OGGv");
    auto raw = loadFixture("83_upload_interval_begin.bin");
    if (raw.getSize() == 0) {
      logMessage("fixture missing -- skipping");
      expect(true);
      return;
    }

    expectEquals((int)raw.getSize(), 25,
                 "the reference client's 0x83 is not 25 bytes");

    NinjamProtocol::IntervalBegin begin;
    expect(NinjamProtocol::parseIntervalBegin(raw, begin));
    expect(begin.isOggAudio(), "reference fourCC is not OGGv");
    expect(begin.username.isEmpty(), "0x83 must carry no username");
    logMessage("reference upload begin: channel " +
               juce::String(begin.channelIndex) + ", estsize " +
               juce::String((int)begin.estimatedSize));

    // Our builder must produce the same 25 bytes.
    auto ours = NinjamProtocol::buildIntervalBegin(
        begin.guid, begin.estimatedSize, begin.fourcc, begin.channelIndex);
    expect(ours == raw, "our UPLOAD_INTERVAL_BEGIN differs from the reference");
  }

  void testUsermaskPacket() {
    beginTest("reference CLIENT_SET_USERMASK layout matches ours");
    auto raw = loadFixture("81_client_set_usermask.bin");
    if (raw.getSize() == 0) {
      logMessage("fixture missing -- skipping");
      expect(true);
      return;
    }

    NinjamProtocol::Reader r(raw.getData(), raw.getSize());
    std::vector<std::pair<juce::String, juce::uint32>> masks;
    while (!r.atEnd()) {
      juce::String name;
      juce::uint32 mask = 0;
      if (!r.cstr(name) || !r.u32le(mask))
        break;
      masks.emplace_back(name, mask);
    }
    expect(!masks.empty(), "no entries in the reference usermask");
    for (const auto &[name, mask] : masks)
      logMessage("reference subscribes to '" + name + "' mask 0x" +
                 juce::String::toHexString((int)mask));

    auto ours = NinjamProtocol::buildUsermask(masks);
    expect(ours == raw, "our CLIENT_SET_USERMASK differs from the reference");
  }

  // -------------------------------------------------------------------
  // A real Ogg bitstream produced by the reference encoder.
  // -------------------------------------------------------------------
  void testReferenceOggDecodes() {
    beginTest("reference Ogg stream decodes to the expected audio");
    auto raw = loadFixture("reference_interval_48000.ogg");
    if (raw.getSize() == 0) {
      logMessage("fixture missing -- skipping");
      expect(true);
      return;
    }

    VorbisDecoder dec;
    std::vector<float> pcm;
    const auto *bytes = static_cast<const juce::uint8 *>(raw.getData());
    for (size_t pos = 0; pos < raw.getSize(); pos += 4096) {
      const int n = (int)std::min((size_t)4096, raw.getSize() - pos);
      dec.decode(bytes + pos, n);
      while (dec.available() > 0) {
        const int avail = dec.available();
        const float *p = dec.pcm();
        pcm.insert(pcm.end(), p, p + avail);
        dec.skip(avail);
      }
    }

    expectEquals(dec.sampleRate(), 48000,
                 "reference stream declares an unexpected sample rate");
    expect(dec.numChannels() >= 1);
    const int frames = (int)pcm.size() / juce::jmax(1, dec.numChannels());
    expect(frames > 1000, "decoded almost nothing from the reference stream");
    logMessage("decoded " + juce::String(frames) + " frames at " +
               juce::String(dec.sampleRate()) + " Hz, " +
               juce::String(dec.numChannels()) + " ch");

    // The reference was transmitting the shared interval probe: a 440 Hz bed
    // with 3 kHz bursts. Both should survive into the decoded audio.
    std::vector<float> left((size_t)frames);
    const int ch = juce::jmax(1, dec.numChannels());
    for (int i = 0; i < frames; ++i)
      left[(size_t)i] = pcm[(size_t)i * ch];

    const int skip = frames / 8;
    const int len = frames - 2 * skip;
    if (len > 4800) {
      const double rms = TestSignal::rms(left.data() + skip, len);
      expect(rms > 0.01, "reference stream decoded to near-silence (rms " +
                             juce::String(rms, 5) + ")");

      auto bursts = TestSignal::findBursts(left.data() + skip, len, 3000.0,
                                           0.008, 48000.0);
      logMessage("found " + juce::String((int)bursts.size()) +
                 " probe bursts, rms " + juce::String(rms, 4));
      expect(!bursts.empty(),
             "no probe bursts found in the reference stream -- our decoder or "
             "the fixture is wrong");
    }
  }
};

static ReferenceFixtureTests referenceFixtureTests;

} // namespace
