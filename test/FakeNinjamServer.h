#pragma once

#include <chalkwalk/ninjam/Bytes.h>

#include <JuceHeader.h>

#include "NinjamProtocol.h"

// Minimal in-process Ninjam server for loopback tests.
//
// Listens on 127.0.0.1 on an ephemeral port, performs the auth handshake, and
// (optionally) echoes every 0x83/0x84 upload straight back as a 0x04/0x05
// download attributed to `echoUsername`. This works because the upload and
// download wire forms are nearly identical: 0x04 is the 25-byte 0x83 payload
// plus a NUL-terminated username, and 0x05 is byte-identical to 0x84. The GUID
// passes through unchanged, so the client's own guidToInterval lookup matches
// its own upload and no bookkeeping is needed here.
//
// NinjamClient needs no modification to be tested against this: connectToServer
// takes a host and port, and run() dials them.
class FakeNinjamServer : private juce::Thread {
public:
  FakeNinjamServer();
  ~FakeNinjamServer() override;

  bool start(int bpm = 120, int bpi = 8);
  void stop();
  int port() const { return boundPort; }

  // Handshake knobs so failure paths can be exercised too.
  void setGrantAccess(bool g) { grantAccess = g; }
  void setChallenge(const juce::uint8 c[8]);
  void setEchoUsername(juce::String u);
  void setEchoEnabled(bool e) { echoEnabled = e; }

  // Server-initiated messages, callable from the test thread.
  void sendServerConfig(int bpm, int bpi);
  void sendUserInfo(const juce::String &user, int chIdx,
                    const juce::String &chName, bool active = true);
  void sendChat(const juce::String &type, const juce::String &p1 = {},
                const juce::String &p2 = {});
  bool sendRaw(juce::uint8 type, const void *data, int size);

  // Observations, all guarded internally.
  struct Received {
    juce::uint8 type;
    chalkwalk::ninjam::ByteBuffer payload;
  };

  bool hasClient() const;
  int countReceived(juce::uint8 type) const;
  juce::Array<Received> messagesOfType(juce::uint8 type) const;
  chalkwalk::ninjam::ByteBuffer lastPayloadOfType(juce::uint8 type) const;
  int completedUploads() const { return uploadsCompleted.load(); }
  void clearReceived();

  const juce::uint8 *challengeBytes() const { return challenge; }

private:
  void run() override;
  void handleClientMessage(juce::uint8 type,
                           const chalkwalk::ninjam::ByteBuffer &payload);
  bool send(juce::uint8 type, const void *data, int size);
  bool readExactly(void *dest, int numBytes);

  juce::StreamingSocket listener;
  std::unique_ptr<juce::StreamingSocket> client;
  mutable juce::CriticalSection clientMutex;
  mutable juce::CriticalSection stateMutex;

  int boundPort = 0;
  std::atomic<bool> grantAccess{true};
  std::atomic<bool> echoEnabled{true};
  std::atomic<int> uploadsCompleted{0};
  juce::String echoUsername{"peer"};
  int serverBpm = 120;
  int serverBpi = 8;
  juce::uint8 challenge[8] = {0, 1, 2, 3, 4, 5, 6, 7};

  juce::Array<Received> received;
};

// Pumps the JUCE message loop until `pred` holds or the timeout expires.
// NinjamClient bounces every listener callback through callAsync, so the loop
// must be pumped even when the condition itself does not depend on it --
// otherwise queued messages pile up and the leak detector fires at teardown.
template <typename Fn> bool waitUntil(Fn &&pred, int timeoutMs = 5000) {
  const auto deadline =
      juce::Time::getMillisecondCounter() + (juce::uint32)timeoutMs;
  while (juce::Time::getMillisecondCounter() < deadline) {
    if (pred())
      return true;
    juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
  }
  return pred();
}
