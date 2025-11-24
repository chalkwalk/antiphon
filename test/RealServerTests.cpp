#include <JuceHeader.h>

#include "FakeNinjamServer.h" // for waitUntil
#include "IntervalClock.h"
#include "NinjamClient.h"
#include "TestSignal.h"

namespace {

// Exercises the client against a REAL ninjamsrv, which the in-process loopback
// cannot cover: the actual server's auth flow, its channel-info handling, and
// whether it accepts and archives our uploads.
//
// Skipped unless NINJAM_TEST_SERVER is set, so the default suite stays
// hermetic. Start a server with scripts/testserver.sh, then:
//
//   NINJAM_TEST_SERVER=127.0.0.1:2049 ./NinjamTests RealServer
//
// After it passes, the uploaded intervals are on disk in the archive
// directory; run scripts/analyze_archive.py over it.

struct Target {
  bool valid = false;
  juce::String host;
  int port = 2049;
};

Target readTarget() {
  Target t;
  const auto env = juce::SystemStats::getEnvironmentVariable(
      "NINJAM_TEST_SERVER", juce::String());
  if (env.isEmpty())
    return t;

  t.valid = true;
  if (env.contains(":")) {
    t.host = env.upToFirstOccurrenceOf(":", false, false);
    t.port = env.fromFirstOccurrenceOf(":", false, false).getIntValue();
  } else {
    t.host = env;
  }
  if (t.port <= 0)
    t.port = 2049;
  return t;
}

struct Recorder : public NinjamClientListener {
  std::atomic<int> connected{0};
  std::atomic<int> disconnected{0};
  std::atomic<int> bpm{0}, bpi{0};
  void onConnected() override { connected.fetch_add(1); }
  void onDisconnected(const juce::String &) override {
    disconnected.fetch_add(1);
  }
  void onServerConfig(int b, int i) override {
    bpm.store(b);
    bpi.store(i);
  }
};

// Newest <timestamp>.ninjam session directory that already has a manifest.
juce::File findSession(const juce::File &archiveDir) {
  juce::Array<juce::File> dirs;
  archiveDir.findChildFiles(dirs, juce::File::findDirectories, false,
                            "*.ninjam");
  juce::File best;
  juce::Time bestTime;
  for (const auto &d : dirs) {
    if (!d.getChildFile("clipsort.log").existsAsFile())
      continue;
    const auto t = d.getCreationTime();
    if (best.getFullPathName().isEmpty() || t > bestTime) {
      best = d;
      bestTime = t;
    }
  }
  return best;
}

class RealServerTests : public juce::UnitTest {
public:
  RealServerTests() : juce::UnitTest("RealServer", "RealServer") {}

  void runTest() override {
    const auto target = readTarget();

    beginTest("real server integration");
    if (!target.valid) {
      logMessage("NINJAM_TEST_SERVER not set -- skipping. Start a server with "
                 "scripts/testserver.sh and set NINJAM_TEST_SERVER=host:port "
                 "to run this.");
      expect(true);
      return;
    }

    logMessage("connecting to " + target.host + ":" +
               juce::String(target.port));

    const double sr = 48000.0;
    NinjamClient client;
    Recorder rec;
    client.addListener(&rec);
    client.setSampleRate(sr);
    client.updateChannelInfo({"testtone"});

    client.connectToServer(target.host, target.port, "anonymous:njtest", "");

    const bool connected =
        waitUntil([&] { return client.isConnected(); }, 10000);
    if (!connected) {
      client.removeListener(&rec);
      client.disconnectFromServer();
      expect(false, "could not connect to " + target.host + ":" +
                        juce::String(target.port) +
                        " -- is scripts/testserver.sh running?");
      return;
    }

    expect(waitUntil([&] { return rec.connected.load() > 0; }),
           "onConnected never fired");

    // The server announces its tempo right after login.
    expect(waitUntil([&] { return rec.bpm.load() > 0; }, 10000),
           "server never sent SERVER_CONFIG_CHANGE");
    logMessage("server tempo: " + juce::String(rec.bpm.load()) + " bpm / " +
               juce::String(rec.bpi.load()) + " bpi");

    const int bpm = rec.bpm.load(), bpi = rec.bpi.load();
    expect(bpm > 0 && bpi > 0);
    client.setServerBpm(bpm);
    client.setServerBpi(bpi);

    IntervalClock clock;
    clock.prepare(sr);
    clock.setTempo(bpm, bpi);
    const int intervalSamples = clock.samplesPerInterval();
    logMessage("interval = " + juce::String(intervalSamples) + " samples");

    // The server only creates an archive session directory on a periodic check
    // that runs every 30 seconds, and only while an authenticated user is
    // connected (ninjamsrv.cpp:1232-1243). Anything uploaded before that check
    // fires is broadcast but never written to disk. So if we are going to
    // verify the archive, wait for the directory to appear first.
    juce::File archiveDir;
    const auto archivePath = juce::SystemStats::getEnvironmentVariable(
        "NINJAM_ARCHIVE", juce::String());
    if (archivePath.isNotEmpty()) {
      archiveDir = juce::File(archivePath);
      logMessage("waiting for the server to open an archive session in " +
                 archivePath + " (up to 45 s -- it checks every 30 s)");
      const bool opened = waitUntil(
          [&] { return !findSession(archiveDir).getFullPathName().isEmpty(); },
          45000);
      expect(opened, "no archive session directory appeared in " + archivePath +
                         " -- is SessionArchive set in the config?");
      if (!opened) {
        client.removeListener(&rec);
        client.disconnectFromServer();
        return;
      }
      logMessage("archive session: " +
                 findSession(archiveDir).getFullPathName());
    }

    // Send three intervals of the same signal the Test Tone toggle produces:
    // a 440 Hz sine with a full-scale impulse at sample 0.
    const int kIntervals = 3;
    for (int n = 0; n < kIntervals; ++n) {
      juce::AudioBuffer<float> buf(2, intervalSamples);
      for (int ch = 0; ch < 2; ++ch) {
        auto *p = buf.getWritePointer(ch);
        for (int i = 0; i < intervalSamples; ++i)
          p[i] = (i == 0) ? 1.0f
                          : 0.25f * (float)std::sin(2.0 * TestSignal::kPi *
                                                    440.0 * i / sr);
      }
      client.processCapturedAudio(buf, intervalSamples, 0, false);
      expect(client.isConnected(),
             "connection dropped while uploading interval " +
                 juce::String(n));
      juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    }

    // Stay connected briefly so the server flushes the archive files.
    waitUntil([] { return false; }, 1500);
    expect(client.isConnected(),
           "server closed the connection after our uploads -- check the "
           "server log for a protocol complaint");

    if (archiveDir.exists()) {
      const auto session = findSession(archiveDir);
      expect(!session.getFullPathName().isEmpty(), "archive session vanished");

      // clipsort.log is written with buffered stdio and is only flushed when
      // the server closes the session, so mid-session it may still be empty.
      // Report it, but assert on the clip files, which are the substantive
      // evidence that the server accepted and stored our uploads.
      const auto log = session.getChildFile("clipsort.log").loadFileAsString();
      int userLines = 0;
      for (const auto &line : juce::StringArray::fromLines(log))
        if (line.startsWith("user ") && line.contains("njtest"))
          ++userLines;
      logMessage("clipsort.log currently lists " + juce::String(userLines) +
                 " of our clips (buffered until the session closes)");

      juce::Array<juce::File> oggs;
      session.findChildFiles(oggs, juce::File::findFiles, true, "*.OGG");
      expect(oggs.size() >= kIntervals,
             "archive holds " + juce::String(oggs.size()) +
                 " OGG files, expected at least " + juce::String(kIntervals));

      int nonEmpty = 0;
      for (const auto &f : oggs)
        if (f.getSize() > 0)
          ++nonEmpty;
      expect(nonEmpty >= kIntervals, "archived OGG files are empty");

      logMessage("archive verified: " + juce::String(oggs.size()) +
                 " OGG clips on disk, all non-empty");
    }

    logMessage("uploaded " + juce::String(kIntervals) +
               " intervals; analyse with scripts/analyze_archive.py");

    client.removeListener(&rec);
    client.disconnectFromServer();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
  }
};

static RealServerTests realServerTests;

} // namespace
