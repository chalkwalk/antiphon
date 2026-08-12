#pragma once

#include "NinjamClient.h"
#include <JuceHeader.h>
#include <functional>

// A bot is a Ninjam client.
//
// Not a server-side fake and not a special case inside NinjamClient: it opens a
// socket and joins a room like any other player. Two things follow, and both
// are the point rather than a side effect.
//
// It can join any server, so the same bots that fill a practice room could sit
// in a real one. `join` takes a host and a port and has no idea which it is.
//
// And it exercises the code you do. A practice room where the other players are
// real clients tests the real path -- the encoder, the relay, the decoder, the
// interval delay, the mixer -- rather than a parallel one built to look like it.
//
// TRANSMIT: the conductor calls renderInterval once per interval, off the audio
// thread, and it goes out through NinjamClient::processCapturedAudio -- the
// same call the plugin makes. That is safe from a non-audio thread by
// construction: it allocates and does file I/O, so it already runs on the
// message thread via callAsync, and writeFull documents being called from
// several threads under a lock (NinjamClient.cpp:199).
//
// LEAVING: a bot must be trivially easy to get rid of. See the rules on
// `part()` below; they live here rather than in PracticeRoom so they hold
// wherever the bot is pointed.
class PracticeBot : private NinjamClientListener {
public:
  // Fills one interval. Called on the conductor thread, never the audio thread,
  // so it may allocate -- though there is no reason for it to.
  using Render = std::function<void(juce::AudioBuffer<float> &buffer,
                                    int numSamples, int intervalIndex)>;

  PracticeBot(juce::String botName, juce::StringArray channelNames);
  ~PracticeBot() override;

  // Silence unless a render is set, which is deliberate: a bot that can join a
  // room and do nothing is the first thing worth proving.
  void setRender(Render r);

  // When this player leaves the room, so does the bot. Empty means nothing but
  // the connection itself ends it. PracticeRoom always sets it.
  void setOwner(juce::String ownerUsername);

  // Whose audio this bot wants. Empty subscribes to nobody, which is the
  // default and what a generative bot wants: it follows the grid, not the room,
  // and an unsubscribed client never causes an interval to be allocated.
  void setListensTo(juce::String username);

  bool join(const juce::String &host, int port, double sampleRate);

  // Idempotent, and safe from any thread. Once parted a bot stays parted --
  // there is no rejoin.
  void part();

  bool isActive() const { return active.load(); }
  const juce::String &name() const { return botName; }
  NinjamClient &client() { return netClient; }

  // Conductor thread. A no-op once parted.
  void renderInterval(int numSamples, int intervalIndex);

  // The commands a bot answers to by private message, from anyone in the room.
  static bool isPartCommand(const juce::String &text);
  static juce::String helpLine(const juce::String &botName);

private:
  void onConnected() override;
  void onDisconnected(const juce::String &reason) override;
  void onUserInfoChange() override;
  void onChatMessage(const juce::String &type, const juce::String &username,
                     const juce::String &text) override;

  juce::String botName;
  juce::StringArray channels;
  juce::String owner;
  juce::String listensTo;
  Render render;

  NinjamClient netClient;
  juce::AudioBuffer<float> renderBuffer;

  std::atomic<bool> active{false};
  std::atomic<bool> sawOwner{false};
  double rate = 48000.0;

  mutable juce::CriticalSection stateMutex;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PracticeBot)
};
