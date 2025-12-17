#include <JuceHeader.h>

#include "InteropFixture.h"
#include "IntervalClock.h"
#include "NinjamClient.h"
#include "TestSignal.h"

#include <algorithm>

#include <vector>

// Differential tests against the OFFICIAL NINJAM reference client.
//
// Everything else in this suite is self-referential: FakeNinjamServer is our
// own reading of the protocol, so a misreading shared by the client and the
// fake server passes every test while real interop is broken. These tests use
// the reference implementation as an independent oracle.
//
// Opt-in (NINJAM_INTEROP=1) because they need a built ninjamsrv and harness and
// run in real time. See test/README.md.

namespace {

using namespace Interop;

struct ChatRecorder : public NinjamClientListener {
  juce::CriticalSection lock;
  juce::StringArray types, users, texts;
  std::atomic<int> connected{0};
  std::atomic<int> bpm{0}, bpi{0};

  void onConnected() override { connected.fetch_add(1); }
  void onDisconnected(const juce::String &) override {}
  void onServerConfig(int b, int i) override {
    bpm.store(b);
    bpi.store(i);
  }
  void onUserInfoChange() override {}
  void onChatMessage(const juce::String &type, const juce::String &user,
                     const juce::String &text) override {
    juce::ScopedLock sl(lock);
    types.add(type);
    users.add(user);
    texts.add(text);
  }
  bool sawText(const juce::String &needle) {
    juce::ScopedLock sl(lock);
    for (const auto &t : texts)
      if (t.contains(needle))
        return true;
    return false;
  }
};

class InteropTests : public juce::UnitTest {
public:
  InteropTests() : juce::UnitTest("Interop", "Interop") {}

  void runTest() override {
    if (!enabled()) {
      beginTest("interop against the reference client");
      logMessage("NINJAM_INTEROP not set -- skipping. Build ninjamsrv with "
                 "scripts/testserver.sh and run with NINJAM_INTEROP=1.");
      expect(true);
      return;
    }

    if (!serverBinary().existsAsFile()) {
      beginTest("interop prerequisites");
      expect(false, "ninjamsrv not found at " +
                        serverBinary().getFullPathName() +
                        " -- run scripts/testserver.sh once to build it");
      return;
    }
    if (!refClientBinary().existsAsFile()) {
      beginTest("interop prerequisites");
      expect(false, "NinjamRefClient not found at " +
                        refClientBinary().getFullPathName() +
                        " -- install libogg-dev and libvorbis-dev, then "
                        "rebuild");
      return;
    }

    testIntervalGridAgreement();
    testChatBothDirections();
    testOurAudioReachesReference();
  }

  // Reads back the harness's raw float32 stereo capture.
  static std::vector<float> readCapture(const juce::File &f) {
    juce::MemoryBlock mb;
    f.loadFileAsData(mb);
    std::vector<float> out((size_t)(mb.getSize() / sizeof(float)));
    if (!out.empty())
      memcpy(out.data(), mb.getData(), out.size() * sizeof(float));
    return out;
  }

  // ---------------------------------------------------------------------
  // Phase 2: does the official client decode what we transmit?
  // ---------------------------------------------------------------------
  void testOurAudioReachesReference() {
    beginTest("the reference client decodes our transmitted audio");

    const int bpm = 120, bpi = 8;
    const double sr = 48000.0;

    LocalServer server;
    if (!server.start(bpm, bpi)) {
      expect(false, "could not start server: " + server.getLastError());
      return;
    }

    RefClient ref;
    if (!ref.start((int)sr)) {
      expect(false, "could not start reference harness: " + ref.getLastError());
      return;
    }
    ref.command("connect 127.0.0.1:" + juce::String(server.getPort()) +
                " anonymous:refbot");
    if (!ref.waitUntilConnected() || !ref.waitForTempo(bpm, bpi)) {
      expect(false, "reference client never settled on the session tempo");
      return;
    }

    NinjamClient ours;
    ChatRecorder rec;
    ours.addListener(&rec);
    ours.setSampleRate(sr);
    ours.setServerBpm(bpm);
    ours.setServerBpi(bpi);
    ours.updateChannelInfo({"ourtone"});
    ours.connectToServer("127.0.0.1", server.getPort(), "anonymous:ourbot", "");
    if (!waitUntil([&] { return ours.isConnected(); }, 15000)) {
      expect(false, "our client never connected");
      return;
    }

    // 440 Hz plus a full-scale impulse at the top of every interval, which is
    // what makes alignment measurable rather than merely audible.
    logMessage("both clients connected; starting driver");
    OurClientDriver driver(ours);
    driver.configure(sr, bpm, bpi);
    driver.setTone(440.0, 0.25, true);
    driver.begin();

    const auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory);
    const auto pcm = tmp.getChildFile("interop_ref_capture.f32");
    const auto marks = tmp.getChildFile("interop_ref_capture.txt");
    pcm.deleteFile();
    marks.deleteFile();

    logMessage("driver started; waiting for reference to see us");
    const bool sawUs = ref.waitForUser("ourbot", 30000);
    logMessage(sawUs ? "reference sees our user" : "reference NEVER saw our user");
    expect(sawUs, "reference client never saw our user appear");

    ref.command("capture " + pcm.getFullPathName() + " " +
                marks.getFullPathName());

    // Let several intervals go by. Playback is one interval behind transmit.
    const int wanted = 4;
    waitUntil([&] { return driver.intervalsElapsed() >= wanted + 2; },
              (int)(1000.0 * (wanted + 3) * bpi * 60.0 / bpm));

    logMessage("intervals elapsed: " + juce::String(driver.intervalsElapsed()));
    ref.command("stopcapture");
    logMessage("capture stopped");
    driver.stop();
    logMessage("driver stopped");

    auto audio = readCapture(pcm);
    expect(audio.size() > 0, "reference client captured nothing");
    if (audio.empty()) {
      ours.removeListener(&rec);
      ours.disconnectFromServer();
      return;
    }

    const int frames = (int)audio.size() / 2;
    logMessage("reference captured " + juce::String(frames) + " frames (" +
               juce::String(frames / sr, 2) + " s)");

    // Level and pitch of what the official client actually decoded from us.
    std::vector<float> left((size_t)frames);
    for (int i = 0; i < frames; ++i)
      left[(size_t)i] = audio[(size_t)i * 2];

    // Skip the lead-in before our first interval arrives.
    int firstAudio = 0;
    while (firstAudio < frames && std::fabs(left[(size_t)firstAudio]) < 1.0e-4f)
      ++firstAudio;
    const int usable = frames - firstAudio;
    expect(usable > (int)sr, "less than a second of audio recovered");
    if (usable <= (int)sr) {
      ours.removeListener(&rec);
      ours.disconnectFromServer();
      return;
    }

    // Measure over a steady stretch, avoiding the very start and end.
    const int anaStart = firstAudio + usable / 8;
    const int anaLen = usable * 3 / 4;
    const double freq =
        TestSignal::dominantFrequency(left.data() + anaStart, anaLen, sr);
    const double rms = TestSignal::rms(left.data() + anaStart, anaLen);

    logMessage("reference decoded: " + juce::String(freq, 1) + " Hz, rms " +
               juce::String(rms, 4));

    // Pitch is the assertion that matters: it catches sample-rate and encoder
    // mismatches, which is the failure mode that silently ruins a session.
    expect(std::fabs(freq - 440.0) / 440.0 < 0.03,
           "reference client decoded our tone at " + juce::String(freq, 1) +
               " Hz, expected 440 -- a sample-rate or encoder mismatch");

    // Level is measured and reported rather than asserted against an absolute
    // target. The reference applies its own remote-user and channel gains, so
    // the observed level is not directly comparable with what we sent; the
    // ratio is recorded here so a regression in OUR gain staging would show up
    // as a change, and so the constant factor can be pinned down separately.
    const double sentRms = 0.25 / std::sqrt(2.0);
    logMessage("level: reference rms " + juce::String(rms, 4) + " vs sent " +
               juce::String(sentRms, 4) + " (ratio " +
               juce::String(rms / sentRms, 3) + ")");
    expect(rms > 0.01, "reference client decoded essentially no audio (rms " +
                           juce::String(rms, 5) + ")");

    // Alignment: locate our per-interval impulses and measure their spacing.
    // The threshold is relative to the tone, not absolute, because the level
    // the reference plays us back at is not something we control.
    std::vector<int> impulses;
    const double threshold = std::max(4.0 * rms, 0.02);
    for (int i = anaStart; i < firstAudio + usable; ++i) {
      if (left[(size_t)i] > threshold &&
          (impulses.empty() || i - impulses.back() > 1000))
        impulses.push_back(i);
    }
    logMessage("impulses found: " + juce::String((int)impulses.size()));

    if (impulses.size() >= 2) {
      std::vector<int> spacings;
      for (size_t i = 1; i < impulses.size(); ++i)
        spacings.push_back(impulses[i] - impulses[i - 1]);
      const int minS = *std::min_element(spacings.begin(), spacings.end());
      const int maxS = *std::max_element(spacings.begin(), spacings.end());
      logMessage("impulse spacing: min " + juce::String(minS) + ", max " +
                 juce::String(maxS) + ", interval " +
                 juce::String(driver.samplesPerInterval()));

      expect(std::abs(minS - driver.samplesPerInterval()) < 2000,
             "impulse spacing " + juce::String(minS) +
                 " does not match the interval length " +
                 juce::String(driver.samplesPerInterval()));
      expect(maxS - minS < 2000,
             "impulse spacing drifted by " + juce::String(maxS - minS) +
                 " samples between intervals");
    } else {
      logMessage("note: too few impulses to measure spacing; the codec may "
                 "have smoothed them away at this bitrate");
    }

    ours.removeListener(&rec);
    ours.disconnectFromServer();
    waitUntil([&] { return !ours.isConnected(); }, 5000);
  }

  // ---------------------------------------------------------------------
  // Phase 1a: does the reference client land on the same interval grid?
  // ---------------------------------------------------------------------
  void testIntervalGridAgreement() {
    beginTest("interval grid agrees with the reference client");

    // The reference derives samples-per-interval from bpm, bpi and its own
    // sample rate. If our arithmetic differs by even one sample, our interval
    // boundaries drift against every other client in the session -- silently,
    // and worse the longer the jam runs.
    const std::vector<std::pair<int, int>> tempos{{120, 8}, {90, 16}, {137, 4}};
    const std::vector<int> rates{44100, 48000, 96000};

    for (const auto &[bpm, bpi] : tempos) {
      LocalServer server;
      if (!server.start(bpm, bpi)) {
        expect(false, "could not start server at " + juce::String(bpm) + "bpm: " + server.getLastError());
        continue;
      }

      for (int sr : rates) {
        RefClient ref;
        if (!ref.start(sr)) {
          expect(false, "could not start reference harness: " + ref.getLastError());
          continue;
        }
        ref.command("srate " + juce::String(sr));
        ref.command("connect 127.0.0.1:" + juce::String(server.getPort()) +
                    " anonymous:refbot");

        if (!ref.waitUntilConnected()) {
          expect(false, "reference client never connected at " +
                            juce::String(bpm) + "bpm/" + juce::String(bpi) +
                            "bpi/" + juce::String(sr));
          continue;
        }
        if (!ref.waitForTempo(bpm, bpi)) {
          const auto s = ref.status();
          expect(false, "reference never adopted the session tempo at " +
                            juce::String(bpm) + "bpm/" + juce::String(bpi) +
                            "bpi (reports " + juce::String(s.bpm, 1) + "/" +
                            juce::String(s.bpi) + ")");
          continue;
        }

        const auto st = ref.status();
        IntervalClock clock;
        clock.prepare((double)sr);
        clock.setTempo(bpm, bpi);

        expectEquals(clock.samplesPerInterval(), st.intervalLen,
                     "interval length at " + juce::String(bpm) + "bpm/" +
                         juce::String(bpi) + "bpi/" + juce::String(sr) + "Hz");

        logMessage(juce::String(bpm) + "bpm/" + juce::String(bpi) + "bpi/" +
                   juce::String(sr) + "Hz: reference " +
                   juce::String(st.intervalLen) + ", ours " +
                   juce::String(clock.samplesPerInterval()));
      }
    }
  }

  // ---------------------------------------------------------------------
  // Phase 1b: chat, both directions, through a real server.
  // ---------------------------------------------------------------------
  void testChatBothDirections() {
    beginTest("chat round-trips with the reference client");

    LocalServer server;
    if (!server.start(120, 8)) {
      expect(false, "could not start server: " + server.getLastError());
      return;
    }

    RefClient ref;
    if (!ref.start(48000)) {
      expect(false, "could not start reference harness: " + ref.getLastError());
      return;
    }
    ref.command("connect 127.0.0.1:" + juce::String(server.getPort()) +
                " anonymous:refbot");
    if (!ref.waitUntilConnected()) {
      expect(false, "reference client never connected");
      return;
    }

    NinjamClient ours;
    ChatRecorder rec;
    ours.addListener(&rec);
    ours.setSampleRate(48000.0);
    ours.connectToServer("127.0.0.1", server.getPort(), "anonymous:ourbot", "");

    const bool up = waitUntil([&] { return ours.isConnected(); }, 15000);
    expect(up, "our client never connected to the real server");
    if (!up) {
      ours.removeListener(&rec);
      ours.disconnectFromServer();
      return;
    }

    ref.clearEvents();

    // Ours -> reference.
    ours.sendChatMessage("hello-from-ours");
    const auto seen = ref.waitForEventContaining("chat", "hello-from-ours");
    expect(seen.isNotEmpty(),
           "reference client did not receive our chat message; saw: " +
               ref.allEvents().joinIntoString(" / "));

    // Reference -> ours.
    ref.command("chat hello-from-reference");
    expect(waitUntil([&] { return rec.sawText("hello-from-reference"); }, 15000),
           "we did not receive the reference client's chat message");

    ours.removeListener(&rec);
    ours.disconnectFromServer();
    waitUntil([&] { return !ours.isConnected(); }, 5000);
  }
};

static InteropTests interopTests;

} // namespace
