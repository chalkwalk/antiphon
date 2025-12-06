#include <JuceHeader.h>

#include "InteropFixture.h"
#include "IntervalClock.h"
#include "NinjamClient.h"

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
          expect(false, "could not start reference harness");
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
      expect(false, "could not start reference harness");
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
