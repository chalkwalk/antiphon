#pragma once

#include "BotAddress.h"
#include "BotChat.h"
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
class PracticeBot : private NinjamClientListener, private juce::Timer {
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

  // Who else this bot arrived with, and what the group is called.
  //
  // Needed only for the arrival roster, and only to decide whether to use the
  // band's NAME: two strangers' bots in one room are a list, not a band, and
  // calling them one would be a small lie in the first line anybody reads.
  // A bot told nothing simply lists whoever it can see.
  void setBandmates(juce::StringArray names, juce::String bandName);

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
  void onRoomMembershipChange(const juce::String &username,
                              bool joined) override;

  // The arrival window: five seconds after connecting, decide whether to
  // announce the band, introduce ourselves, or stay quiet.
  void timerCallback() override;
  int arrivalDelayMs() const;
  static bool isOwnerName(const juce::String &username,
                          const juce::String &ownerName);

  // Every bot in the room right now, ours or not, sorted so that every bot
  // computes the same list and therefore the same answer.
  juce::StringArray botsPresent() const;
  void onChatMessage(const juce::String &type, const juce::String &username,
                     const juce::String &text) override;


  // The subset that needs no address, because its SYNTAX is unmistakable: a
  // `[key: Dm]` tag and a `| Am | F |` chart. Nobody writes either by accident,
  // and both are things the whole band must agree about, so they are acted on
  // wherever they appear and answered by nobody.
  //
  // Deliberately excludes `shake`, which is an ordinary English word and needs
  // to be aimed at somebody.
  bool handleStructured(const juce::String &text,
                        const juce::String &username);

  // The room as the addressing engine understands it: who is here, which of
  // them are bots, what each is called and what their channel is named. Built
  // fresh per message, because it is small and staleness here means answering
  // somebody who has left.
  BotAddress::Room currentRoom() const;



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

  // The arrival choreography (docs/BOT-CHAT.md section 6).
  //
  // A bot announces the band unless somebody has already announced IT.
  //
  // Self-referential on purpose. A bot cannot know whether it is the first to
  // arrive -- the membership list has not come through when `onConnected`
  // fires, so every bot sees an empty room -- but it can always know whether it
  // has been introduced, because that is observed rather than inferred. One
  // question covers the ordinary startup, a bot arriving an hour late, and a
  // band whose other members never connected.
  std::atomic<bool> announcedMe{false};
  std::atomic<bool> arrivalDone{false};
  juce::StringArray bandmates;
  juce::String bandName;

  // One conversation, with one person. Belongs to whoever opened it, not to
  // the room -- two other people talking are not talking to the bot.
  BotAddress::Attention attention;

  // Where the key and the chart CAME FROM, which a bot must say when it
  // reports either. Both always have a value -- a room starts in C major and a
  // key implies a chart -- so reporting one without its provenance tells the
  // room it agreed on something nobody chose. Tracked here because only this
  // class sees the message that changed them.
  BotAnswer::Source keySource = BotAnswer::Source::Defaulted;
  juce::String keySetBy;
  BotAnswer::Source chartSource = BotAnswer::Source::Defaulted;

  // Told to stop talking. Per bot rather than per band, so one voice can be
  // hushed without silencing the room -- and atomic because it is read on
  // every message and written from the same thread that reads it.
  std::atomic<bool> chatMuted{false};

  // The room and this bot, in the shape the pure answering code takes.
  BotChat::Context currentContext() const;

  std::atomic<bool> active{false};
  std::atomic<bool> sawOwner{false};
  double rate = 48000.0;

  mutable juce::CriticalSection stateMutex;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PracticeBot)
};
