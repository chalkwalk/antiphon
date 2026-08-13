#pragma once

#include "BotBand.h"
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

  // Play an instrument, and follow the room while doing it.
  //
  // Everything a band member needs to know arrives over the wire -- tempo and
  // BPI from SERVER_CONFIG_CHANGE, the key from a `[key: ...]` chat line, the
  // chords from a Jamtaba-style `| Am | F | C | G |` -- so a bot that follows
  // does so wherever it is pointed, with no orchestrator to tell it. That is
  // why this lives here and not in PracticeRoom.
  void playAs(BotBand::Voice voice, const MusicalKey::Key &key, int bpm,
              int bpi, double sampleRate, std::uint32_t seed);

  // A fresh seed, so the figures change. What "shake" does.
  void shake();

  BotBand::Settings currentSettings() const;
  bool isPlaying() const { return playing.load(); }

  // The commands a bot answers to, beyond parting.
  static bool isShakeCommand(const juce::String &text);

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
  void onServerConfig(int bpm, int bpi) override;
  void onUserInfoChange() override;
  void onChatMessage(const juce::String &type, const juce::String &username,
                     const juce::String &text) override;

  // Returns true if the line was an instruction to the band. Room chat and
  // private messages take the same commands.
  bool handleBandCommand(const juce::String &text);

  // Instructions to ONE player, which room chat deliberately does not take.
  //
  // The key and the chords are things the whole band must agree about, so they
  // are shouted. What instrument the soloist is holding is nobody else's
  // business, and "guitar" is a word that turns up in ordinary conversation --
  // a room where saying it silently reconfigures a bot is a room with a
  // poltergeist in it. Returns a reply, or an empty string for "not for me".
  juce::String handlePrivateCommand(const juce::String &text);

  // False once the bot has parted because its owner left.
  bool checkOwnerStillHere();

  juce::String botName;
  juce::StringArray channels;
  juce::String owner;
  juce::String listensTo;
  Render render;

  BotBand::Voice bandVoice = BotBand::Voice::Drums;
  BotBand::Settings settings;
  std::atomic<bool> playing{false};

  NinjamClient netClient;
  juce::AudioBuffer<float> renderBuffer;

  std::atomic<bool> active{false};
  std::atomic<bool> sawOwner{false};
  double rate = 48000.0;

  mutable juce::CriticalSection stateMutex;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PracticeBot)
};
