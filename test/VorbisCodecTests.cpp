#include <JuceHeader.h>

#include "TestSignal.h"
#include "VorbisCodec.h"

#include <cstdint>
#include <vector>

namespace {

// Encodes interleaved frames and returns the complete Ogg stream, including the
// end-of-stream flush.
std::vector<uint8_t> encodeAll(const float *interleaved, int numFrames,
                               int sampleRate, int numChannels,
                               int bitrateKbps = 128) {
  VorbisEncoder enc(sampleRate, numChannels, bitrateKbps, 12345);
  std::vector<uint8_t> out;

  auto drain = [&]() {
    while (enc.available() > 0) {
      const int n = enc.available();
      const auto *p = static_cast<const uint8_t *>(enc.data());
      out.insert(out.end(), p, p + n);
      enc.advance(n);
    }
  };

  drain(); // the constructor emits the three header pages eagerly

  const int block = 1024;
  for (int pos = 0; pos < numFrames; pos += block) {
    const int n = std::min(block, numFrames - pos);
    enc.encode(interleaved + (size_t)pos * numChannels, n);
    drain();
  }

  enc.encode(nullptr, 0);
  drain();
  return out;
}

struct Decoded {
  std::vector<float> interleaved;
  int sampleRate = 0;
  int numChannels = 0;
  int numFrames() const {
    return numChannels > 0 ? (int)interleaved.size() / numChannels : 0;
  }
};

Decoded decodeAll(const std::vector<uint8_t> &bytes, int chunkSize = 4096) {
  VorbisDecoder dec;
  Decoded d;

  for (size_t pos = 0; pos < bytes.size(); pos += (size_t)chunkSize) {
    const int n = (int)std::min((size_t)chunkSize, bytes.size() - pos);
    dec.decode(bytes.data() + pos, n);
    while (dec.available() > 0) {
      const int avail = dec.available();
      const float *p = dec.pcm();
      d.interleaved.insert(d.interleaved.end(), p, p + avail);
      dec.skip(avail);
    }
  }

  d.sampleRate = dec.sampleRate();
  d.numChannels = dec.numChannels();
  return d;
}

class VorbisCodecTests : public juce::UnitTest {
public:
  VorbisCodecTests() : juce::UnitTest("VorbisCodec", "VorbisCodec") {}

  void runTest() override {
    beginTest("encoder honours its constructed sample rate");
    // The unit-level companion to the NinjamClient TX bug: a stream encoded at
    // rate R must declare rate R, or every listener resamples it wrongly.
    for (int sr : {44100, 48000, 88200, 96000}) {
      auto pcm = TestSignal::makeSine(sr / 4, 2, 440.0, (double)sr, 0.5f);
      auto bytes = encodeAll(pcm.data(), sr / 4, sr, 2);
      auto d = decodeAll(bytes);
      expectEquals(d.sampleRate, sr,
                   "declared rate wrong for encoder at " + juce::String(sr));
      expectEquals(d.numChannels, 2);
    }

    beginTest("stereo round-trip preserves level and pitch");
    {
      const int sr = 48000, frames = sr; // one second
      auto pcm = TestSignal::makeSine(frames, 2, 440.0, sr, 0.5f);
      auto d = decodeAll(encodeAll(pcm.data(), frames, sr, 2));

      expect(d.numFrames() > frames / 2, "decoder returned too little audio");

      // Skip the first and last 10% to avoid codec ramp-in/out.
      const int skip = d.numFrames() / 10;
      const int n = d.numFrames() - 2 * skip;
      const float *left = d.interleaved.data() + (size_t)skip * 2;

      const double inRms = TestSignal::rms(pcm.data(), frames, 2);
      const double outRms = TestSignal::rms(left, n, 2);
      const double deltaDb =
          TestSignal::toDb(outRms) - TestSignal::toDb(inRms);
      expect(std::fabs(deltaDb) < 1.0,
             "level moved by " + juce::String(deltaDb, 2) + " dB");

      const double freq = TestSignal::dominantFrequency(left, n, sr, 2);
      expect(std::fabs(freq - 440.0) / 440.0 < 0.02,
             "measured " + juce::String(freq, 1) + " Hz, expected 440");
    }

    beginTest("decoded frame count is close to encoded");
    {
      const int sr = 48000, frames = 24000;
      auto pcm = TestSignal::makeSine(frames, 2, 440.0, sr, 0.5f);
      auto d = decodeAll(encodeAll(pcm.data(), frames, sr, 2));
      const double ratio = (double)d.numFrames() / (double)frames;
      expect(ratio > 0.98 && ratio < 1.02,
             "got " + juce::String(d.numFrames()) + " of " +
                 juce::String(frames) + " frames");
    }

    beginTest("mono round-trip");
    {
      const int sr = 48000, frames = 24000;
      auto pcm = TestSignal::makeSine(frames, 1, 440.0, sr, 0.5f);
      auto d = decodeAll(encodeAll(pcm.data(), frames, sr, 1));
      expectEquals(d.numChannels, 1);
      expectEquals(d.sampleRate, sr);

      const int skip = d.numFrames() / 10;
      const int n = d.numFrames() - 2 * skip;
      const double freq = TestSignal::dominantFrequency(
          d.interleaved.data() + skip, n, sr, 1);
      expect(std::fabs(freq - 440.0) / 440.0 < 0.02,
             "measured " + juce::String(freq, 1) + " Hz");
    }

    beginTest("truncated multi-page stream yields partial audio");
    {
      // Noise is incompressible, so ten seconds of it spans many Ogg pages and
      // a truncated prefix decodes to roughly the corresponding fraction.
      const int sr = 48000, frames = sr * 10;
      juce::Random rng(7);
      std::vector<float> pcm((size_t)frames * 2);
      for (auto &v : pcm)
        v = (float)(rng.nextDouble() * 2.0 - 1.0) * 0.5f;

      auto bytes = encodeAll(pcm.data(), frames, sr, 2);
      auto full = decodeAll(bytes);
      expectEquals(full.numFrames(), frames);

      auto truncated = bytes;
      truncated.resize(truncated.size() / 2);
      auto d = decodeAll(truncated);
      const double fraction = (double)d.numFrames() / (double)frames;
      expect(fraction > 0.3 && fraction < 0.7,
             "half a stream decoded to " + juce::String(fraction * 100.0, 1) +
                 "% of the audio");
    }

    beginTest("short compressible stream is a single page (all-or-nothing)");
    {
      // Load-bearing property of interval delivery, so it is pinned rather than
      // assumed. ogg_stream_pageout only emits a page once roughly 4 kB has
      // accumulated, so a quiet or tonal interval produces NO decodable audio
      // until the end-of-stream flush. A receiver therefore cannot start
      // playing an interval early just because some WRITE chunks have arrived;
      // it must wait for the final chunk. If this test ever starts failing, the
      // paging behaviour changed and the interval buffering assumptions in
      // NinjamClient need revisiting.
      const int sr = 48000, frames = sr; // one second of pure tone
      auto pcm = TestSignal::makeSine(frames, 2, 440.0, sr, 0.5f);
      auto bytes = encodeAll(pcm.data(), frames, sr, 2);

      auto truncated = bytes;
      truncated.resize(truncated.size() * 99 / 100);
      auto d = decodeAll(truncated);
      expectEquals(d.numFrames(), 0,
                   "expected no audio before the final page");

      expectEquals(decodeAll(bytes).numFrames(), frames);
    }

    beginTest("garbage input does not crash or produce audio");
    {
      juce::Random rng(42);
      std::vector<uint8_t> junk(4096);
      for (auto &b : junk)
        b = (uint8_t)rng.nextInt(256);
      auto d = decodeAll(junk);
      expectEquals(d.numFrames(), 0);
    }

    beginTest("header-only stream produces no audio");
    {
      VorbisEncoder enc(48000, 2, 128, 1);
      std::vector<uint8_t> headers;
      while (enc.available() > 0) {
        const int n = enc.available();
        const auto *p = static_cast<const uint8_t *>(enc.data());
        headers.insert(headers.end(), p, p + n);
        enc.advance(n);
      }
      expect(!headers.empty(), "constructor emitted no header pages");
      auto d = decodeAll(headers);
      expectEquals(d.sampleRate, 48000);
      expectEquals(d.numChannels, 2);
      expectEquals(d.numFrames(), 0);
    }

    beginTest("interval timing probe survives the codec");
    {
      // Timing markers are only useful if they come back where they went in.
      // A single-sample impulse does not survive a perceptual codec, so the
      // probe uses short enveloped tone bursts instead. This pins that they
      // are recoverable, and located to within a millisecond, after a real
      // encode/decode round trip -- the property the interop timing tests and
      // the archive analysis both depend on.
      const int sr = 48000;
      const int intervalLen = sr * 2; // 2 s "interval"
      TestSignal::IntervalProbe probe;

      std::vector<float> pcm((size_t)intervalLen * 2);
      for (int i = 0; i < intervalLen; ++i) {
        const float v = probe.sampleAt(i, intervalLen, i, sr);
        pcm[(size_t)i * 2] = v;
        pcm[(size_t)i * 2 + 1] = v;
      }

      auto d = decodeAll(encodeAll(pcm.data(), intervalLen, sr, 2));
      expect(d.numFrames() > intervalLen / 2, "codec returned too little");

      std::vector<float> left((size_t)d.numFrames());
      for (int i = 0; i < d.numFrames(); ++i)
        left[(size_t)i] = d.interleaved[(size_t)i * 2];

      auto found = TestSignal::findBursts(left.data(), (int)left.size(),
                                          probe.burstHz, probe.burstSeconds,
                                          sr);
      expectEquals((int)found.size(), (int)probe.positions.size(),
                   "expected one detected burst per probe position");

      if (found.size() == probe.positions.size()) {
        const double tolerance = 0.001 * sr; // 1 ms
        for (size_t i = 0; i < found.size(); ++i) {
          const int expectedAt =
              (int)(probe.positions[i] * (double)intervalLen);
          expect(std::abs(found[i] - expectedAt) < tolerance,
                 "burst " + juce::String((int)i) + " found at " +
                     juce::String(found[i]) + ", expected near " +
                     juce::String(expectedAt));
        }
      }
    }

    beginTest("decoder tolerates single-byte feeding");
    {
      const int sr = 48000, frames = 4800;
      auto pcm = TestSignal::makeSine(frames, 2, 440.0, sr, 0.5f);
      auto bytes = encodeAll(pcm.data(), frames, sr, 2);
      auto d = decodeAll(bytes, 1);
      expectEquals(d.sampleRate, sr);
      const double ratio = (double)d.numFrames() / (double)frames;
      expect(ratio > 0.98 && ratio < 1.02,
             "byte-at-a-time decode gave " + juce::String(d.numFrames()) +
                 " frames");
    }
  }
};

static VorbisCodecTests vorbisCodecTests;

} // namespace
