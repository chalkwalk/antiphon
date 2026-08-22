#pragma once

#include "NinjamClient.h"
#include <BotClient.h>

#include <algorithm>

// Antiphon's `NinjamClient`, as the bots see it.
//
// The whole of what ties the band to this plugin's client, and it is one class
// with no logic in it: names, types and the direction of a callback. Everything
// a bot decides is on the other side of the interface, where it can be moved
// and tested without a socket.
//
// The conversions are all this does, and they are not incidental -- they are
// the JUCE boundary. `juce::String` in, `std::string` out, and an
// `AudioBuffer` built around the caller's pointers rather than copied.
class NinjamBotClient final : public BotClient::Client,
                              private NinjamClientListener {
public:
  NinjamBotClient() { client.addListener(this); }

  ~NinjamBotClient() override {
    client.removeListener(this);
    client.disconnectFromServer();
  }

  void addListener(BotClient::Listener *l) override { listeners.push_back(l); }
  void removeListener(BotClient::Listener *l) override {
    listeners.erase(std::remove(listeners.begin(), listeners.end(), l),
                    listeners.end());
  }

  void setSampleRate(double rate) override { client.setSampleRate(rate); }

  void setChannels(const std::vector<std::string> &names) override {
    juce::StringArray out;
    for (const auto &n : names)
      out.add(juce::String(n));
    client.updateChannelInfo(out);
  }

  void setDefaultRecvEnabled(bool enabled) override {
    client.setDefaultRecvEnabled(enabled);
  }

  void connect(const std::string &host, int port, const std::string &username,
               const std::string &password) override {
    client.connectToServer(juce::String(host), port, juce::String(username),
                           juce::String(password));
  }

  void disconnect() override { client.disconnectFromServer(); }
  bool isConnected() const override { return client.isConnected(); }

  std::vector<BotClient::Member> members() const override {
    std::vector<BotClient::Member> out;
    for (const auto &m : client.getRoomMembers())
      out.push_back({m.username.toStdString(), m.channelCount});
    return out;
  }

  std::vector<BotClient::Peer> peers() const override {
    std::vector<BotClient::Peer> out;
    for (const auto &[name, user] : client.getRemoteUsers()) {
      BotClient::Peer peer;
      peer.username = name.toStdString();
      for (const auto &[index, channel] : user.channels)
        peer.channels.push_back({index, channel.channelName.toStdString(),
                                 channel.recvEnabled});
      out.push_back(std::move(peer));
    }
    return out;
  }

  void setRecv(const std::string &username, int channelIndex,
               bool enabled) override {
    client.setRemoteUserRecv(juce::String(username), channelIndex, enabled);
  }

  void sendChat(const std::string &text) override {
    client.sendChatMessage(juce::String(text));
  }

  void sendPrivate(const std::string &to, const std::string &text) override {
    client.sendPrivateMessage(juce::String(to), juce::String(text));
  }

  std::unique_ptr<BotClient::Timer> createTimer(
      std::function<void()> onFire) override {
    return std::make_unique<MessageThreadTimer>(std::move(onFire));
  }

  void transmit(const float *left, const float *right,
                int numSamples) override {
    if (left == nullptr || numSamples <= 0)
      return;
    // Wrapped rather than copied: the caller already owns this memory for the
    // duration of the call, and an interval is several seconds of audio.
    float *channels[2] = {const_cast<float *>(left),
                          const_cast<float *>(right != nullptr
                                                  ? right
                                                  : left)};
    juce::AudioBuffer<float> view(channels, right != nullptr ? 2 : 1,
                                  numSamples);
    client.processCapturedAudio(view, numSamples, 0, false);
  }

private:
  // A juce::Timer is the message thread's own, which is where NinjamClient
  // delivers every callback -- so a bot's timers and its messages stay on one
  // thread, exactly as they were before the interface existed.
  class MessageThreadTimer final : public BotClient::Timer,
                                   private juce::Timer {
  public:
    explicit MessageThreadTimer(std::function<void()> fn)
        : onFire(std::move(fn)) {}
    ~MessageThreadTimer() override { stopTimer(); }

    void start(int delayMs) override { startTimer(delayMs); }
    void stop() override { stopTimer(); }
    bool isRunning() const override { return isTimerRunning(); }

  private:
    void timerCallback() override {
      // One-shot: stop before firing, so a callback that starts it again wins
      // rather than being cancelled by its own return.
      stopTimer();
      if (onFire)
        onFire();
    }
    std::function<void()> onFire;
  };

  // NinjamClient calls these; the bots hear the versions above.
  void onConnected() override { each([](auto *l) { l->onConnected(); }); }

  void onDisconnected(const juce::String &reason) override {
    const auto why = reason.toStdString();
    each([&](auto *l) { l->onDisconnected(why); });
  }

  void onServerConfig(int bpm, int bpi) override {
    each([&](auto *l) { l->onServerConfig(bpm, bpi); });
  }

  void onUserInfoChange() override {
    each([](auto *l) { l->onUserInfoChange(); });
  }

  void onRoomMembershipChange(const juce::String &username,
                              bool joined) override {
    const auto who = username.toStdString();
    each([&](auto *l) { l->onRoomMembershipChange(who, joined); });
  }

  void onChatMessage(const juce::String &type, const juce::String &username,
                     const juce::String &text) override {
    const auto t = type.toStdString(), u = username.toStdString(),
               m = text.toStdString();
    each([&](auto *l) { l->onChatMessage(t, u, m); });
  }

  template <typename Fn> void each(Fn fn) {
    // A copy, because a listener may remove itself while being called -- which
    // is exactly what a bot does when it is told to leave.
    const auto snapshot = listeners;
    for (auto *l : snapshot)
      fn(l);
  }

  NinjamClient client;
  std::vector<BotClient::Listener *> listeners;
};
