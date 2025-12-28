#pragma once

#include <JuceHeader.h>

#include "FakeNinjamServer.h" // waitUntil
#include "IntervalClock.h"
#include "NinjamClient.h"
#include "TestSignal.h"

#include <cmath>
#include <utility>
#include <vector>

// Fixtures for the interop tests: a real ninjamsrv and the reference-client
// harness, both launched as child processes and driven from the test.
//
// TEMPORARY, alongside test/refclient. See test/README.md.

namespace Interop {

// Interop tests are opt-in: they need a built ninjamsrv and the reference
// harness, and they run in real time (an interval at 120 bpm / 8 bpi is four
// seconds). NINJAM_INTEROP=1 turns them on.
inline bool enabled() {
  return juce::SystemStats::getEnvironmentVariable("NINJAM_INTEROP", "")
      .isNotEmpty();
}

inline juce::File serverBinary() {
  const auto env =
      juce::SystemStats::getEnvironmentVariable("NINJAM_SERVER_BIN", "");
  if (env.isNotEmpty())
    return juce::File(env);
  return juce::File("/tmp/njbuild/ninjam/server/ninjamsrv");
}

inline juce::File refClientBinary() {
  const auto env =
      juce::SystemStats::getEnvironmentVariable("NINJAM_REFCLIENT_BIN", "");
  if (env.isNotEmpty())
    return juce::File(env);
  // Sits in <build>/test/refclient/. Walk up from the test binary, which may be
  // one level deeper in multi-config builds (.../NinjamTests_artefacts/Debug/).
  auto dir = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                 .getParentDirectory();
  for (int i = 0; i < 4 && dir.exists(); ++i) {
    const auto candidate =
        dir.getChildFile("refclient").getChildFile("NinjamRefClient");
    if (candidate.existsAsFile())
      return candidate;
    dir = dir.getParentDirectory();
  }
  return {};
}

// Asks the OS for a free TCP port by binding and immediately releasing one.
inline int freePort() {
  juce::StreamingSocket probe;
  if (!probe.createListener(0, "127.0.0.1"))
    return 0;
  const int port = probe.getBoundPort();
  probe.close();
  return port;
}

// A local ninjamsrv with a generated config, so each test picks its own tempo.
class LocalServer {
public:
  ~LocalServer() { stop(); }

  bool start(int bpm, int bpi) {
    port = freePort();
    if (port <= 0)
      return false;

    tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                  .getChildFile("njinterop_" + juce::String(port));
    tempDir.createDirectory();

    auto cfg = tempDir.getChildFile("server.cfg");
    cfg.replaceWithText(
        "Port " + juce::String(port) + "\n"
        "AnonymousUsers multi\n"
        "AnonymousUsersCanChat yes\n"
        "MaxChannels 32 32\n"
        "MaxUsers 8\n"
        "DefaultBPI " + juce::String(bpi) + "\n"
        "DefaultBPM " + juce::String(bpm) + "\n"
        "ACL 0.0.0.0/0 allow\n");

    if (!serverBinary().existsAsFile()) {
      lastError = "server binary missing: " + serverBinary().getFullPathName();
      return false;
    }

    juce::StringArray argv{serverBinary().getFullPathName(),
                           cfg.getFullPathName()};
    if (!proc.start(argv)) {
      lastError = "ChildProcess::start failed for " + argv.joinIntoString(" ");
      return false;
    }

    // Wait for it to accept connections.
    if (waitUntil(
            [this] {
              juce::StreamingSocket s;
              return s.connect("127.0.0.1", port, 200);
            },
            8000))
      return true;

    // Note: readAllProcessOutput() blocks until the child exits, so it must
    // never be called on a process that is still running.
    const bool alive = proc.isRunning();
    if (alive)
      proc.kill();
    lastError = "server did not accept connections on port " +
                juce::String(port) +
                (alive ? "; process was still running"
                       : "; process had already exited: " +
                             proc.readAllProcessOutput().substring(0, 400));
    return false;
  }

  juce::String getLastError() const { return lastError; }

  void stop() {
    if (proc.isRunning())
      proc.kill();
    if (tempDir.exists())
      tempDir.deleteRecursively();
  }

  int getPort() const { return port; }

private:
  juce::ChildProcess proc;
  juce::File tempDir;
  juce::String lastError;
  int port = 0;
};

// The reference client harness. It dials back to a listener we own, so we never
// have to discover a port it chose.
class RefClient {
public:
  ~RefClient() { stop(); }

  bool start(int sampleRate = 48000, int blockSize = 512) {
    const int controlPort = freePort();
    if (controlPort <= 0)
      return false;
    if (!listener.createListener(controlPort, "127.0.0.1"))
      return false;
    if (!refClientBinary().existsAsFile())
      return false;

    if (!proc.start(refClientBinary().getFullPathName().quoted() +
                    " --control-port " + juce::String(controlPort) +
                    " --srate " + juce::String(sampleRate) + " --block " +
                    juce::String(blockSize)))
      return false;

    // Bounded: waitForNextConnection() alone blocks forever if the harness
    // never dials back, which turns any startup failure into a hung test run.
    if (listener.waitUntilReady(true, 10000) != 1) {
      const bool alive = proc.isRunning();
      if (alive)
        proc.kill();
      lastError = "harness never connected to the control port" +
                  juce::String(alive ? "; process was still running"
                                     : "; process had already exited: " +
                                           proc.readAllProcessOutput()
                                               .substring(0, 400));
      return false;
    }
    conn.reset(listener.waitForNextConnection());
    if (conn == nullptr) {
      lastError = "control connection was not accepted";
      return false;
    }

    // The harness announces itself once its audio thread is up.
    if (waitForEvent("ready", 5000).isNotEmpty())
      return true;
    lastError = "harness connected but never signalled ready";
    return false;
  }

  juce::String getLastError() const { return lastError; }

  void stop() {
    if (conn != nullptr && conn->isConnected())
      send("quit");
    if (proc.isRunning() && !proc.waitForProcessToFinish(2000))
      proc.kill();
    conn.reset();
    listener.close();
  }

  void send(const juce::String &line) {
    if (conn == nullptr || !conn->isConnected())
      return;
    const auto s = line + "\n";
    conn->write(s.toRawUTF8(), (int)s.getNumBytesAsUTF8());
  }

  // Sends a command and returns its "OK ..." reply, or an empty string.
  juce::String command(const juce::String &line, int timeoutMs = 5000) {
    send(line);
    return waitForReply(timeoutMs);
  }

  // Blocks until a line starting with "OK" or "ERR" arrives.
  juce::String waitForReply(int timeoutMs = 5000) {
    juce::String out;
    waitUntil(
        [&] {
          pump();
          for (int i = 0; i < replies.size(); ++i) {
            out = replies[i];
            replies.remove(i);
            return true;
          }
          return false;
        },
        timeoutMs);
    return out;
  }

  // Blocks until an "EV <kind> ..." line arrives; returns the whole line.
  juce::String waitForEvent(const juce::String &kind, int timeoutMs = 5000) {
    juce::String out;
    waitUntil(
        [&] {
          pump();
          for (int i = 0; i < events.size(); ++i) {
            if (events[i].startsWith("EV " + kind)) {
              out = events[i];
              events.remove(i);
              return true;
            }
          }
          return false;
        },
        timeoutMs);
    return out;
  }

  // Waits for an event of `kind` that also contains `needle`. A session
  // generates plenty of unrelated chat traffic (TOPIC on join, JOIN/PART for
  // every user), so matching on kind alone finds the wrong line.
  juce::String waitForEventContaining(const juce::String &kind,
                                      const juce::String &needle,
                                      int timeoutMs = 15000) {
    juce::String out;
    waitUntil(
        [&] {
          pump();
          for (int i = 0; i < events.size(); ++i) {
            if (events[i].startsWith("EV " + kind) &&
                events[i].contains(needle)) {
              out = events[i];
              return true;
            }
          }
          return false;
        },
        timeoutMs);
    return out;
  }

  juce::StringArray allEvents() {
    pump();
    return events;
  }

  void clearEvents() {
    pump();
    events.clear();
  }

  struct Status {
    int status = -99;
    double bpm = 0.0;
    int bpi = 0;
    int intervalLen = 0;
    int intervalPos = 0;
    int loop = -1;
    int numUsers = 0;
    bool valid = false;
  };

  // "OK status <code> <bpm> <bpi> <len> <pos> <loop> <users>"
  Status status() {
    Status s;
    const auto reply = command("status");
    auto tok = juce::StringArray::fromTokens(reply, " ", "");
    if (tok.size() >= 9 && tok[1] == "status") {
      s.status = tok[2].getIntValue();
      s.bpm = tok[3].getDoubleValue();
      s.bpi = tok[4].getIntValue();
      s.intervalLen = tok[5].getIntValue();
      s.intervalPos = tok[6].getIntValue();
      s.loop = tok[7].getIntValue();
      s.numUsers = tok[8].getIntValue();
      s.valid = true;
    }
    return s;
  }

  // Waits until the harness reports it is connected and has a tempo.
  bool waitUntilConnected(int timeoutMs = 15000) {
    return waitUntil(
        [this] {
          const auto s = status();
          return s.valid && s.status == 0 && s.intervalLen > 0;
        },
        timeoutMs);
  }

  // NJClient latches the server's bpm/bpi only at its next interval boundary
  // (njclient.cpp:788-811), so immediately after auth it still reports its
  // built-in defaults. Wait for the session tempo to actually take effect
  // before reading anything derived from it.
  bool waitForTempo(int bpm, int bpi, int timeoutMs = 30000) {
    return waitUntil(
        [&] {
          const auto s = status();
          return s.valid && s.status == 0 && (int)std::lround(s.bpm) == bpm &&
                 s.bpi == bpi && s.intervalLen > 0;
        },
        timeoutMs);
  }

  // "OK hasuser <0|1> <peak>"
  bool hasUser(const juce::String &namePart, float *peakOut = nullptr) {
    const auto reply = command("hasuser " + namePart, 3000);
    auto tok = juce::StringArray::fromTokens(reply, " ", "");
    if (tok.size() < 4 || tok[1] != "hasuser")
      return false;
    if (peakOut != nullptr)
      *peakOut = (float)tok[3].getDoubleValue();
    return tok[2].getIntValue() == 1;
  }

  bool waitForUser(const juce::String &namePart, int timeoutMs = 30000) {
    return waitUntil([&] { return hasUser(namePart); }, timeoutMs);
  }

private:
  void pump() {
    if (conn == nullptr)
      return;
    while (conn->waitUntilReady(true, 0) == 1) {
      char buf[2048];
      const int n = conn->read(buf, sizeof(buf), false);
      if (n <= 0)
        break;
      pending += juce::String::fromUTF8(buf, n);
    }
    int nl;
    while ((nl = pending.indexOfChar('\n')) >= 0) {
      const auto line = pending.substring(0, nl).trim();
      pending = pending.substring(nl + 1);
      if (line.isEmpty())
        continue;
      if (line.startsWith("EV "))
        events.add(line);
      else
        replies.add(line);
    }
  }

  juce::ChildProcess proc;
  juce::StreamingSocket listener;
  std::unique_ptr<juce::StreamingSocket> conn;
  juce::String pending, lastError;
  juce::StringArray replies, events;
};

// Drives our NinjamClient the way PluginProcessor::processBlock does: an
// IntervalClock advanced in blocks, transmitting one complete interval at each
// boundary and mixing decoded remote audio every block.
//
// PluginProcessor itself cannot be linked into the test target (it needs the
// JucePlugin_* defines), so this mirrors its loop rather than reusing it. Keep
// the two in step -- see test/README.md.
class OurClientDriver : private juce::Thread {
public:
  explicit OurClientDriver(NinjamClient &c)
      : juce::Thread("OurClientDriver"), client(c) {}
  ~OurClientDriver() override { stop(); }

  void configure(double sr, int bpm, int bpi, int blk = 512) {
    sampleRate = sr;
    blockSize = blk;
    clock.prepare(sr);
    clock.setTempo(bpm, bpi);
    clock.reset();
    intervalLen = clock.samplesPerInterval();
  }

  void setTone(double freq, double amp, bool useIntervalProbe) {
    toneFreq = freq;
    toneAmp = amp;
    probe.bedHz = freq;
    probe.bedAmp = (float)amp;
    useProbe = useIntervalProbe;
  }

  const TestSignal::IntervalProbe &getProbe() const { return probe; }

  void setCapture(bool on) { capturing = on; }

  void begin() { startThread(); }

  void stop() {
    signalThreadShouldExit();
    stopThread(3000);
  }

  int samplesPerInterval() const { return intervalLen; }

  // Decoded remote audio, interleaved stereo, and where interval boundaries
  // fell within it.
  struct Capture {
    std::vector<float> audio;
    std::vector<std::pair<int64_t, int>> markers; // (sample offset, interval #)
  };

  Capture takeCapture() {
    juce::ScopedLock sl(captureLock);
    Capture c;
    c.audio.swap(capture.audio);
    c.markers.swap(capture.markers);
    return c;
  }

  int intervalsElapsed() const { return intervalCount.load(); }

private:
  void run() override {
    std::vector<IntervalClock::Event> events;
    std::vector<float> txAccum;
    txAccum.reserve((size_t)intervalLen * 2);

    juce::AudioBuffer<float> outBlock(2, blockSize);
    const double inc = 2.0 * 3.14159265358979323846 * toneFreq / sampleRate;
    nextBlockDueMs = juce::Time::getMillisecondCounterHiRes();

    while (!threadShouldExit()) {
      const int64_t posAtBlockStart = clock.samplePosInInterval();
      events.clear();
      clock.advance(blockSize, events);

      int boundaryOffset = -1;
      for (const auto &e : events)
        if (e.type == IntervalClock::Event::Type::IntervalStart)
          boundaryOffset = e.sampleOffset;

      // Generate this block of the transmit signal on the interval grid.
      std::vector<float> block((size_t)blockSize);
      for (int i = 0; i < blockSize; ++i) {
        const int64_t pos =
            intervalLen > 0 ? (posAtBlockStart + i) % intervalLen : 0;
        block[(size_t)i] =
            useProbe ? probe.sampleAt(pos, intervalLen, sampleCounter + i,
                                      sampleRate)
                     : (float)(toneAmp *
                               std::sin(inc * (double)(sampleCounter + i)));
      }

      if (boundaryOffset >= 0) {
        txAccum.insert(txAccum.end(), block.begin(),
                       block.begin() + boundaryOffset);

        if ((int)txAccum.size() == intervalLen && client.isConnected()) {
          juce::AudioBuffer<float> tx(2, intervalLen);
          for (int ch = 0; ch < 2; ++ch)
            memcpy(tx.getWritePointer(ch), txAccum.data(),
                   (size_t)intervalLen * sizeof(float));
          client.processCapturedAudio(tx, intervalLen, 0, false);
        }
        txAccum.clear();
        txAccum.insert(txAccum.end(), block.begin() + boundaryOffset,
                       block.end());

        client.swapIntervalBuffers();
        intervalCount.fetch_add(1);

        if (capturing) {
          juce::ScopedLock sl(captureLock);
          capture.markers.emplace_back(
              (int64_t)capture.audio.size() / 2 + boundaryOffset,
              intervalCount.load());
        }
      } else {
        txAccum.insert(txAccum.end(), block.begin(), block.end());
      }

      outBlock.clear();
      client.getDecodedAudio(outBlock);

      if (capturing) {
        juce::ScopedLock sl(captureLock);
        const float *l = outBlock.getReadPointer(0);
        const float *r = outBlock.getReadPointer(1);
        for (int i = 0; i < blockSize; ++i) {
          capture.audio.push_back(l[i]);
          capture.audio.push_back(r[i]);
        }
      }

      sampleCounter += blockSize;

      // Pace against an absolute schedule, not a fixed sleep. A block of 512
      // at 48 kHz is 10.667 ms; sleeping a truncated 10 ms runs 6.7% fast, and
      // against a real-time peer that eventually outruns the sender and starves
      // an interval. The drop is correct client behaviour -- there is nothing
      // to play -- but it is the harness's clock causing it, not the client.
      nextBlockDueMs += 1000.0 * (double)blockSize / sampleRate;
      const double waitMs = nextBlockDueMs - juce::Time::getMillisecondCounterHiRes();
      if (waitMs > 0.0)
        juce::Thread::sleep((int)std::ceil(waitMs));
      else if (waitMs < -500.0)
        nextBlockDueMs = juce::Time::getMillisecondCounterHiRes(); // resync
    }
  }

  NinjamClient &client;
  IntervalClock clock;
  double sampleRate = 48000.0;
  int blockSize = 512;
  int intervalLen = 0;
  double toneFreq = 440.0, toneAmp = 0.0;
  bool useProbe = false;
  TestSignal::IntervalProbe probe;
  std::atomic<bool> capturing{false};
  std::atomic<int> intervalCount{0};
  int64_t sampleCounter = 0;
  double nextBlockDueMs = 0.0;

  juce::CriticalSection captureLock;
  Capture capture;
};

} // namespace Interop
