#include <JuceHeader.h>

#include "Sha1.h"

#include <string>

namespace {

juce::String toHex(const uint8_t digest[20]) {
  juce::String s;
  for (int i = 0; i < 20; ++i)
    s += juce::String::toHexString((int)digest[i]).paddedLeft('0', 2);
  return s;
}

juce::String hashOf(const std::string &input) {
  Sha1 sha;
  sha.add(input.data(), (int)input.size());
  uint8_t digest[20];
  sha.result(digest);
  return toHex(digest);
}

class Sha1Tests : public juce::UnitTest {
public:
  Sha1Tests() : juce::UnitTest("Sha1", "Sha1") {}

  void runTest() override {
    beginTest("FIPS 180-1 vectors");
    expectEquals(hashOf("abc"),
                 juce::String("a9993e364706816aba3e25717850c26c9cd0d89d"));
    expectEquals(
        hashOf("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
        juce::String("84983e441c3bd26ebaae4aa1f95129e5e54670f1"));
    expectEquals(hashOf(std::string(1000000, 'a')),
                 juce::String("34aa973cd4c4daa4f61eeb2bdbad27316534016f"));

    beginTest("empty input");
    expectEquals(hashOf(""),
                 juce::String("da39a3ee5e6b4b0d3255bfef95601890afd80709"));

    beginTest("incremental add equals monolithic");
    // The auth path feeds SHA1 in several add() calls, so this invariant is
    // load-bearing. Exercise every split point, including across the internal
    // 64-byte block boundary.
    const std::string msg =
        "the quick brown fox jumps over the lazy dog, repeatedly, until this "
        "string is comfortably longer than one sha1 block of sixty-four bytes";
    const juce::String whole = hashOf(msg);
    for (size_t split = 0; split <= msg.size(); ++split) {
      Sha1 sha;
      sha.add(msg.data(), (int)split);
      sha.add(msg.data() + split, (int)(msg.size() - split));
      uint8_t digest[20];
      sha.result(digest);
      if (toHex(digest) != whole) {
        expect(false, "split at " + juce::String((int)split) + " differs");
        break;
      }
    }
    expect(true);

    beginTest("result() resets state for reuse");
    Sha1 sha;
    sha.add("abc", 3);
    uint8_t first[20];
    sha.result(first);
    sha.add("abc", 3);
    uint8_t second[20];
    sha.result(second);
    expectEquals(toHex(second), toHex(first));

    beginTest("zero-length add is a no-op");
    Sha1 a;
    a.add("abc", 3);
    a.add("", 0);
    uint8_t d[20];
    a.result(d);
    expectEquals(toHex(d),
                 juce::String("a9993e364706816aba3e25717850c26c9cd0d89d"));
  }
};

static Sha1Tests sha1Tests;

} // namespace
