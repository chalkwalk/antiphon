// antiphon-bandlab: turn every knob in the band, and hear it.
//
// The voice lab renders a WAV and prints numbers, which is the right tool for
// establishing a fact and the wrong one for finding a sound. Finding a sound is
// dozens of small moves with a listen after each, and a loop of edit-a-constant
// / rebuild / render / open is far too slow to converge -- so it did not
// converge. It went through me instead, one adjustment per message, and neither
// of us can hear what the other is talking about.
//
// This is the same renderer with a control surface on it. Every parameter is a
// slider, the band loops while you move them, and it re-renders in the
// background so a change is audible within about a second.
//
// TWO THINGS PER CONTROL, and the second is the one that matters. A slider sets
// the VALUE, and two boxes beside it set the RANGE -- the span a seed is
// allowed to pick inside. The value is what sounded right today; the range is
// the claim about the instrument, and it is the thing that can only be
// established by listening to both of its ends. `Save` writes both.
//
// A development instrument, not a shipped one: built, never installed, and
// listed in ROADMAP.md as something to retire once the voices settle. It links
// the band's own sources, so what it plays is what the room plays -- the same
// figures, the same harmony, the same interval wrapping.

#include <JuceHeader.h>

#include "AudioMeasure.h"
#include <BandPatch.h>
#include <BotBand.h>
#include "MusicalKey.h"

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBars = 4;

juce::String twoDigits(double v) {
  // Frequencies want no decimals and mix levels want three, and one format for
  // both reads badly. Scale decides.
  const double a = std::abs(v);
  if (a >= 1000.0)
    return juce::String(v, 0);
  if (a >= 10.0)
    return juce::String(v, 2);
  return juce::String(v, 4);
}

// ---------------------------------------------------------------------------

// One control: a name, a slider, its two limits, and three buttons for the
// bottom, middle and top of them.
//
// The buttons exist because that is how a range is actually judged. You do not
// decide "9 to 16 cents" by sweeping a slider; you listen to 9, listen to 16,
// and ask whether both of them are still the instrument. One click each.
// A fader that spans MORE than the bot's range, and shows the difference.
//
// The fader used to span exactly lo..hi, which meant it could never be moved
// outside them -- so shift-click could only ever shrink a range and widening
// one meant typing. The fader now spans the shipped range widened by half its
// width at each end, and the part the seed may actually reach is drawn inside
// it: shaded where the bot cannot go, with a marker at the value a middling
// draw lands on.
class RangeSlider : public juce::Slider {
public:
  double lo = 0.0, hi = 1.0, centre = 0.0;
  bool centreSet = false;

  void paint(juce::Graphics &g) override {
    juce::Slider::paint(g);

    const int y = 0, h = getHeight();
    const float xLo = (float)getPositionOfValue(lo);
    const float xHi = (float)getPositionOfValue(hi);

    // Out of the seed's reach. Drawn over the track rather than instead of it,
    // so the fader still reads as one continuous control that happens to have
    // a usable middle.
    g.setColour(juce::Colours::black.withAlpha(0.45f));
    const float left = (float)getPositionOfValue(getMinimum());
    const float right = (float)getPositionOfValue(getMaximum());
    if (xLo > left)
      g.fillRect(juce::Rectangle<float>(left, (float)y, xLo - left, (float)h));
    if (right > xHi)
      g.fillRect(juce::Rectangle<float>(xHi, (float)y, right - xHi, (float)h));

    // The ends themselves, so a range that has been narrowed to nothing is
    // still visible.
    g.setColour(juce::Colours::white.withAlpha(0.55f));
    g.drawVerticalLine((int)xLo, (float)y, (float)(y + h));
    g.drawVerticalLine((int)xHi, (float)y, (float)(y + h));

    // Where the middle SOUNDS. Hollow until somebody has said so, so an
    // unlistened range and a judged one do not look alike.
    const float xMid = (float)getPositionOfValue(centre);
    juce::Path tri;
    tri.addTriangle(xMid - 4.0f, (float)y, xMid + 4.0f, (float)y, xMid,
                    (float)y + 5.0f);
    g.setColour(juce::Colours::orange);
    if (centreSet)
      g.fillPath(tri);
    else
      g.strokePath(tri, juce::PathStrokeType(1.0f));
  }
};

class KnobRow : public juce::Component {
public:
  std::function<void()> onChange;

  KnobRow() {
    addAndMakeVisible(nameLabel);
    nameLabel.setJustificationType(juce::Justification::centredLeft);

    addAndMakeVisible(slider);
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 78, 20);
    slider.onValueChange = [this] {
      if (knob.value == nullptr || updating)
        return;
      *knob.value = slider.getValue();
      if (onChange)
        onChange();
    };

    for (auto *b : {&lowButton, &midButton, &highButton}) {
      addAndMakeVisible(*b);
      b->setConnectedEdges(juce::Button::ConnectedOnLeft |
                           juce::Button::ConnectedOnRight);
    }
    // Click GOES to an end; shift-click SETS that end from where the fader is.
    //
    // Setting is the operation that actually matters -- a range is arrived at
    // by moving the fader until it stops sounding right and pinning it there --
    // and doing it by reading the number off the slider and typing it into a
    // box is enough friction to stop anybody doing it.
    lowButton.setButtonText("|<");
    midButton.setButtonText("<>");
    highButton.setButtonText(">|");
    lowButton.setTooltip("Go to the low end. Shift: set it from the fader.");
    midButton.setTooltip("Go to the sonic centre -- the value a middling "
                         "random draw lands on. Shift: set it from the fader.");
    highButton.setTooltip("Go to the high end. Shift: set it from the fader.");
    lowButton.onClick = [this] { endButton(End::Low); };
    midButton.onClick = [this] { endButton(End::Centre); };
    highButton.onClick = [this] { endButton(End::High); };

    for (auto *e : {&lowEditor, &highEditor}) {
      addAndMakeVisible(*e);
      e->setJustification(juce::Justification::centred);
      e->onReturnKey = [this] { commitRange(); };
      e->onFocusLost = [this] { commitRange(); };
    }
  }

  void bind(const BandPatch::Knob &k, double outerLow, double outerHigh) {
    knob = k;
    outerLo = outerLow;
    outerHi = outerHigh;
    nameLabel.setText(k.name, juce::dontSendNotification);
    refresh();
  }

  void refresh() {
    if (knob.value == nullptr)
      return;
    updating = true;
    // The FADER spans wider than the range, so a range can be widened by
    // moving the fader past its end and pinning it there.
    slider.setRange(juce::jmin(outerLo, knob.range->lo),
                    juce::jmax(outerHi, knob.range->hi), 0.0);
    slider.lo = knob.range->lo;
    slider.hi = knob.range->hi;
    slider.centre = knob.range->mid();
    slider.centreSet = knob.range->centreSet();
    slider.setValue(*knob.value, juce::dontSendNotification);
    lowEditor.setText(twoDigits(knob.range->lo), juce::dontSendNotification);
    highEditor.setText(twoDigits(knob.range->hi), juce::dontSendNotification);
    // A centre nobody has set is shown as the plain marker it is, so "not
    // listened to yet" and "deliberately in the middle" do not look alike.
    midButton.setButtonText(knob.range->centreSet() ? "<*>" : "<>");
    updating = false;
  }

  void resized() override {
    auto r = getLocalBounds().reduced(2, 1);
    nameLabel.setBounds(r.removeFromLeft(120));
    lowEditor.setBounds(r.removeFromLeft(62).reduced(1));
    lowButton.setBounds(r.removeFromLeft(26));
    midButton.setBounds(r.removeFromLeft(26));
    highButton.setBounds(r.removeFromLeft(26));
    highEditor.setBounds(r.removeFromRight(62).reduced(1));
    slider.setBounds(r);
  }

private:
  enum class End { Low, Centre, High };

  void endButton(End end) {
    if (knob.value == nullptr)
      return;

    if (juce::ModifierKeys::getCurrentModifiers().isShiftDown()) {
      const double here = slider.getValue();
      switch (end) {
      case End::Low:
        if (here < knob.range->hi)
          knob.range->lo = here;
        break;
      case End::High:
        if (here > knob.range->lo)
          knob.range->hi = here;
        break;
      case End::Centre:
        if (here > knob.range->lo && here < knob.range->hi)
          knob.range->centre = here;
        break;
      }
      *knob.value = knob.range->clamp(*knob.value);
    } else {
      const double u = end == End::Low ? 0.0 : (end == End::High ? 1.0 : 0.5);
      *knob.value = knob.range->at(u);
    }

    refresh();
    if (onChange)
      onChange();
  }

  void commitRange() {
    if (knob.value == nullptr)
      return;
    const double lo = lowEditor.getText().getDoubleValue();
    const double hi = highEditor.getText().getDoubleValue();

    // A range with its ends the wrong way round makes a slider that cannot be
    // moved, so it is refused rather than accepted and puzzled over later.
    if (hi > lo) {
      knob.range->lo = lo;
      knob.range->hi = hi;
      *knob.value = knob.range->clamp(*knob.value);
    }
    refresh();
    if (onChange)
      onChange();
  }

  BandPatch::Knob knob;
  double outerLo = 0.0, outerHi = 1.0;
  juce::Label nameLabel;
  RangeSlider slider;
  juce::TextButton lowButton, midButton, highButton;
  juce::TextEditor lowEditor, highEditor;
  bool updating = false;
};

// ---------------------------------------------------------------------------

// Renders the band off the message thread and hands finished buffers to the
// audio callback.
//
// Double-buffered with an atomic index rather than a lock, because the audio
// thread must not wait for a render that takes a second and a half. The render
// thread fills whichever buffer is not being played and then publishes it; the
// audio thread reads the published index and nothing else. That is the same
// discipline the plugin uses to hand remote audio to its own audio thread.
class BandPlayer : public juce::Thread {
public:
  BandPlayer() : juce::Thread("band render") {}

  ~BandPlayer() override { stopThread(2000); }

  struct Report {
    float peak = 0.0f;
    double lufs = 0.0;
    double brightness = 0.0;
    double rmsDb = 0.0;
    bool valid = false;
  };

  // Called from the message thread. Copies what it needs, so the caller may go
  // on editing immediately.
  void request(const BandPatch::Band &band, BotBand::Voice voice, bool solo,
               const juce::String &keyName, int bpm, int bpi,
               std::uint32_t seed) {
    {
      const juce::ScopedLock sl(requestLock);
      pending = band;
      pendingVoice = voice;
      pendingSolo = solo;
      pendingKey = keyName;
      pendingBpm = bpm;
      pendingBpi = bpi;
      pendingSeed = seed;
      haveRequest = true;
    }
    notify();
  }

  void run() override {
    while (!threadShouldExit()) {
      BandPatch::Band band;
      BotBand::Voice voice = BotBand::Voice::Keys;
      bool solo = false;
      juce::String keyName;
      int bpm = 120, bpi = 8;
      std::uint32_t seed = 1;

      {
        const juce::ScopedLock sl(requestLock);
        if (!haveRequest) {
          const juce::ScopedUnlock su(requestLock);
          wait(200);
          continue;
        }
        band = pending;
        voice = pendingVoice;
        solo = pendingSolo;
        keyName = pendingKey;
        bpm = pendingBpm;
        bpi = pendingBpi;
        seed = pendingSeed;
        haveRequest = false;
      }

      render(band, voice, solo, keyName, bpm, bpi, seed);
    }
  }

  // Audio thread. Never blocks and never allocates.
  void readInto(juce::AudioBuffer<float> &out, int numSamples) {
    const int which = published.load();
    if (which < 0) {
      out.clear();
      return;
    }

    const auto &src = buffers[which];
    const int length = src.getNumSamples();
    if (length <= 0) {
      out.clear();
      return;
    }

    int done = 0;
    int pos = position;
    while (done < numSamples) {
      const int chunk = juce::jmin(numSamples - done, length - pos);
      for (int ch = 0; ch < out.getNumChannels(); ++ch)
        out.copyFrom(ch, done, src, juce::jmin(ch, src.getNumChannels() - 1),
                     pos, chunk);
      pos += chunk;
      done += chunk;
      if (pos >= length)
        pos = 0;
    }
    position = pos;
  }

  Report lastReport() {
    const juce::ScopedLock sl(reportLock);
    return report;
  }

  std::function<void()> onRendered;

private:
  void render(BandPatch::Band &band, BotBand::Voice voice, bool solo,
              const juce::String &keyName, int bpm, int bpi,
              std::uint32_t seed) {
    auto key = MusicalKey::parseName(keyName.toStdString());
    if (!key.valid)
      key = MusicalKey::parseName("C major");

    const int n = (int)(kSampleRate * 60.0 / (double)bpm) * bpi;
    if (n <= 0)
      return;

    // `published` is -1 until something has been rendered, which `readInto`
    // guards for and this did not: `1 - -1` is 2, and there are two buffers.
    // The first render therefore wrote through a reference past the end of the
    // array and freed a pointer that was never allocated, so the lab aborted
    // before it could draw a single control.
    const int last = published.load();
    const int which = (last == 0) ? 1 : 0;
    auto &target = buffers[which];
    target.setSize(2, n * kBars, false, false, true);
    target.clear();

    juce::AudioBuffer<float> one(2, n);

    for (int interval = 0; interval < kBars; ++interval) {
      for (int v = 0; v < BotBand::kNumVoices; ++v) {
        const auto thisVoice = (BotBand::Voice)v;
        if (solo && thisVoice != voice)
          continue;

        // Seeded the way PracticeRoom seeds each bot, so the figures are the
        // ones the room would produce.
        std::uint32_t voiceSeed = seed;
        for (int step = 0; step < v; ++step)
          voiceSeed = voiceSeed * 1664525u + 1013904223u;

        auto settings =
            BotBand::defaults(key, bpm, bpi, kSampleRate, voiceSeed);
        settings.usePatchOverrides = true;
        settings.keysPatchOverride = band.keysPatch();
        settings.bassPatchOverride = band.bassPatch();
        settings.leadPatchOverride = band.lead;
        for (int t = 0; t < BotBand::kNumVoices; ++t)
          settings.trimOverride[t] = band.trim[t];

        one.clear();
        BotBand::renderInterval(thisVoice, settings, interval,
                                one.getWritePointer(0), one.getWritePointer(1),
                                n);
        if (!BotBand::isStereo(thisVoice))
          one.copyFrom(1, 0, one, 0, 0, n);

        // The far end applies kDefaultRemoteChannelVolume to every remote
        // channel, so mix at that level or this is 12 dB hotter than the room.
        const float mix = solo ? 1.0f : 0.25f;
        for (int ch = 0; ch < 2; ++ch)
          target.addFrom(ch, interval * n, one, ch, 0, n, mix);
      }

      if (threadShouldExit())
        return;
    }

    {
      const juce::ScopedLock sl(reportLock);
      const int total = target.getNumSamples();
      report.peak = target.getMagnitude(0, total);
      report.lufs = AudioMeasure::integratedLufs(target.getReadPointer(0),
                                                 target.getReadPointer(1),
                                                 total, kSampleRate);
      report.brightness = AudioMeasure::brightnessHz(target.getReadPointer(0),
                                                     total, kSampleRate);
      report.rmsDb = AudioMeasure::toDb(
          AudioMeasure::rms(target.getReadPointer(0), total));
      report.valid = true;
    }

    published.store(which);
    if (onRendered)
      juce::MessageManager::callAsync(onRendered);
  }

  juce::AudioBuffer<float> buffers[2];
  std::atomic<int> published{-1};
  int position = 0;

  juce::CriticalSection requestLock, reportLock;
  BandPatch::Band pending;
  BotBand::Voice pendingVoice = BotBand::Voice::Keys;
  bool pendingSolo = false;
  juce::String pendingKey = "C major";
  int pendingBpm = 120, pendingBpi = 8;
  std::uint32_t pendingSeed = 12345;
  bool haveRequest = false;
  Report report;
};

// ---------------------------------------------------------------------------

class BandLabComponent : public juce::AudioAppComponent {
public:
  BandLabComponent() {
    band = BandPatch::defaults();

    setAudioChannels(0, 2);
    player.startThread();
    player.onRendered = [this] { showReport(); };

    addAndMakeVisible(voiceBox);
    voiceBox.addItem("Kit (not yet)", 1);
    voiceBox.addItem("Bass", 2);
    voiceBox.addItem("Keys", 3);
    voiceBox.addItem("Lead", 4);
    voiceBox.setSelectedId(3);
    voiceBox.onChange = [this] { rebuildRows(); };

    addAndMakeVisible(selectionBox);
    selectionBox.onChange = [this] { selectionChanged(); };

    addAndMakeVisible(playButton);
    playButton.setButtonText("Play");
    playButton.setClickingTogglesState(true);
    playButton.onClick = [this] {
      playing = playButton.getToggleState();
      playButton.setButtonText(playing ? "Stop" : "Play");
    };

    addAndMakeVisible(soloButton);
    soloButton.setButtonText("Solo this voice");
    soloButton.setClickingTogglesState(true);
    soloButton.onClick = [this] { rerender(); };

    addAndMakeVisible(seedLabel);
    seedLabel.setText("seed", juce::dontSendNotification);
    addAndMakeVisible(seedEditor);
    seedEditor.setText("12345");
    seedEditor.onReturnKey = [this] { rerender(); };

    addAndMakeVisible(keyLabel);
    keyLabel.setText("key", juce::dontSendNotification);
    addAndMakeVisible(keyEditor);
    keyEditor.setText("C major");
    keyEditor.onReturnKey = [this] { rerender(); };

    addAndMakeVisible(readout);
    readout.setJustificationType(juce::Justification::centredLeft);

    addAndMakeVisible(saveButton);
    saveButton.setButtonText("Save...");
    saveButton.onClick = [this] { save(); };

    addAndMakeVisible(loadButton);
    loadButton.setButtonText("Load...");
    loadButton.onClick = [this] { load(); };

    addAndMakeVisible(viewport);
    viewport.setViewedComponent(&rows, false);
    viewport.setScrollBarsShown(true, false);

    // The mix, always visible: a level is only ever judged against the other
    // three, so putting it behind a tab would be putting it out of reach at
    // the moment it is needed.
    for (int v = 0; v < BotBand::kNumVoices; ++v) {
      addAndMakeVisible(trimLabels[v]);
      trimLabels[v].setText(BotBand::voiceName((BotBand::Voice)v),
                            juce::dontSendNotification);
      trimLabels[v].setJustificationType(juce::Justification::centred);

      addAndMakeVisible(trimSliders[v]);
      trimSliders[v].setSliderStyle(juce::Slider::LinearVertical);
      trimSliders[v].setTextBoxStyle(juce::Slider::TextBoxBelow, false, 56, 18);
      trimSliders[v].setRange(0.0, 3.0, 0.0);
      trimSliders[v].setValue(band.trim[v], juce::dontSendNotification);
      trimSliders[v].onValueChange = [this, v] {
        band.trim[v] = trimSliders[v].getValue();
        rerender();
      };
    }

    rebuildRows();
    setSize(1000, 700);
  }

  ~BandLabComponent() override { shutdownAudio(); }

  void prepareToPlay(int, double) override {}
  void releaseResources() override {}

  void getNextAudioBlock(const juce::AudioSourceChannelInfo &info) override {
    if (!playing.load()) {
      info.clearActiveBufferRegion();
      return;
    }
    juce::AudioBuffer<float> slice(info.buffer->getArrayOfWritePointers(),
                                   info.buffer->getNumChannels(),
                                   info.startSample, info.numSamples);
    player.readInto(slice, info.numSamples);
  }

  void paint(juce::Graphics &g) override {
    g.fillAll(juce::Colour(0xff1b1f24));
  }

  void resized() override {
    auto r = getLocalBounds().reduced(8);

    auto top = r.removeFromTop(30);
    voiceBox.setBounds(top.removeFromLeft(140));
    top.removeFromLeft(6);
    selectionBox.setBounds(top.removeFromLeft(160));
    top.removeFromLeft(12);
    playButton.setBounds(top.removeFromLeft(80));
    top.removeFromLeft(6);
    soloButton.setBounds(top.removeFromLeft(140));
    top.removeFromLeft(12);
    keyLabel.setBounds(top.removeFromLeft(30));
    keyEditor.setBounds(top.removeFromLeft(100));
    top.removeFromLeft(8);
    seedLabel.setBounds(top.removeFromLeft(36));
    seedEditor.setBounds(top.removeFromLeft(80));

    r.removeFromTop(6);
    readout.setBounds(r.removeFromTop(22));
    r.removeFromTop(6);

    auto bottom = r.removeFromBottom(34);
    saveButton.setBounds(bottom.removeFromLeft(90));
    bottom.removeFromLeft(6);
    loadButton.setBounds(bottom.removeFromLeft(90));

    auto mix = r.removeFromRight(260);
    auto mixLabels = mix.removeFromTop(20);
    const int each = mix.getWidth() / BotBand::kNumVoices;
    for (int v = 0; v < BotBand::kNumVoices; ++v) {
      trimLabels[v].setBounds(mixLabels.removeFromLeft(each));
      trimSliders[v].setBounds(mix.removeFromLeft(each).reduced(4, 0));
    }

    r.removeFromRight(8);
    viewport.setBounds(r);
    rows.setSize(viewport.getWidth() - 12, (int)rowWidgets.size() * 26);
    layoutRows();
  }

private:
  BotBand::Voice currentVoice() const {
    return (BotBand::Voice)(voiceBox.getSelectedId() - 1);
  }

  void rebuildSelectionBox() {
    selectionBox.clear(juce::dontSendNotification);
    switch (currentVoice()) {
    case BotBand::Voice::Keys:
      for (int c = 0; c < 3; ++c)
        selectionBox.addItem(
            BotVoice::padCharacterName((BotVoice::PadCharacter)c), c + 1);
      selectionBox.setSelectedId((int)band.keysCharacter + 1,
                                 juce::dontSendNotification);
      break;
    case BotBand::Voice::Bass:
      for (int t = 0; t < 3; ++t)
        selectionBox.addItem(
            BotVoice::bassTechniqueName((BotVoice::BassTechnique)t), t + 1);
      selectionBox.setSelectedId((int)band.bassTechnique + 1,
                                 juce::dontSendNotification);
      break;
    case BotBand::Voice::Lead:
      for (int i = 0; i < 3; ++i)
        selectionBox.addItem(
            BotVoice::leadInstrumentName((BotVoice::LeadInstrument)i), i + 1);
      selectionBox.setSelectedId((int)band.lead.instrument + 1,
                                 juce::dontSendNotification);
      break;
    case BotBand::Voice::Drums:
      selectionBox.addItem("kit", 1);
      selectionBox.setSelectedId(1, juce::dontSendNotification);
      break;
    }
  }

  void selectionChanged() {
    const int id = selectionBox.getSelectedId();
    if (id <= 0)
      return;
    switch (currentVoice()) {
    case BotBand::Voice::Keys:
      band.keysCharacter = (BotVoice::PadCharacter)(id - 1);
      break;
    case BotBand::Voice::Bass:
      band.bassTechnique = (BotVoice::BassTechnique)(id - 1);
      break;
    case BotBand::Voice::Lead:
      band.lead.instrument = (BotVoice::LeadInstrument)(id - 1);
      break;
    case BotBand::Voice::Drums:
      break;
    }
    rebuildRows();
  }

  void rebuildRows() {
    rebuildSelectionBox();

    const auto knobs = BandPatch::knobsFor(band, currentVoice());

    // The fader's extent comes from the SHIPPED range, not the current one, so
    // it stays put while a range is edited. Anchoring it to the live range
    // would shrink the fader every time the range was narrowed, and there would
    // be no way back out.
    static BandPatch::Band shipped = BandPatch::defaults();
    shipped.keysCharacter = band.keysCharacter;
    shipped.bassTechnique = band.bassTechnique;
    shipped.lead.instrument = band.lead.instrument;
    const auto shippedKnobs = BandPatch::knobsFor(shipped, currentVoice());

    rowWidgets.clear();
    for (size_t i = 0; i < knobs.size(); ++i) {
      const auto &knob = knobs[i];
      // Half the shipped width beyond each end. Not below zero when the
      // shipped range was not: a negative level or decay time is not a sound
      // to go looking for, where a negative detune is.
      double outerLo = knob.range->lo, outerHi = knob.range->hi;
      if (i < shippedKnobs.size() && shippedKnobs[i].range != nullptr) {
        const auto &sr = *shippedKnobs[i].range;
        const double span = sr.hi - sr.lo;
        outerLo = sr.lo - 0.5 * span;
        outerHi = sr.hi + 0.5 * span;
        if (sr.lo >= 0.0)
          outerLo = juce::jmax(0.0, outerLo);
      }

      auto row = std::make_unique<KnobRow>();
      row->bind(knob, outerLo, outerHi);
      row->onChange = [this] { rerender(); };
      rows.addAndMakeVisible(*row);
      rowWidgets.push_back(std::move(row));
    }

    rows.setSize(juce::jmax(400, viewport.getWidth() - 12),
                 (int)rowWidgets.size() * 26);
    layoutRows();
    rerender();
  }

  void layoutRows() {
    int y = 0;
    for (auto &row : rowWidgets) {
      row->setBounds(0, y, rows.getWidth(), 25);
      y += 26;
    }
  }

  void rerender() {
    player.request(band, currentVoice(), soloButton.getToggleState(),
                   keyEditor.getText(), 120, 8,
                   (std::uint32_t)seedEditor.getText().getLargeIntValue());
  }

  void showReport() {
    const auto r = player.lastReport();
    if (!r.valid)
      return;
    readout.setText("peak " + juce::String(r.peak, 3) + "    " +
                        juce::String(r.rmsDb, 1) + " dBFS    " +
                        juce::String(r.lufs, 1) + " LUFS    brightness " +
                        juce::String(r.brightness, 0) + " Hz",
                    juce::dontSendNotification);
  }

  void save() {
    chooser = std::make_unique<juce::FileChooser>(
        "Save these settings",
        juce::File::getSpecialLocation(juce::File::userHomeDirectory)
            .getChildFile("band-patch.txt"),
        "*.txt");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode |
                             juce::FileBrowserComponent::canSelectFiles,
                         [this](const juce::FileChooser &fc) {
                           const auto file = fc.getResult();
                           if (file == juce::File())
                             return;
                           file.replaceWithText(BandPatch::write(band));
                           readout.setText("wrote " + file.getFullPathName(),
                                           juce::dontSendNotification);
                         });
  }

  void load() {
    chooser = std::make_unique<juce::FileChooser>(
        "Load settings",
        juce::File::getSpecialLocation(juce::File::userHomeDirectory), "*.txt");
    chooser->launchAsync(
        juce::FileBrowserComponent::openMode |
            juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser &fc) {
          const auto file = fc.getResult();
          if (file == juce::File())
            return;
          std::string error;
          if (!BandPatch::read(file.loadFileAsString().toStdString(), band,
                               error)) {
            readout.setText(error, juce::dontSendNotification);
            return;
          }
          for (int v = 0; v < BotBand::kNumVoices; ++v)
            trimSliders[v].setValue(band.trim[v], juce::dontSendNotification);
          rebuildRows();
        });
  }

  BandPatch::Band band;
  BandPlayer player;
  std::atomic<bool> playing{false};

  juce::ComboBox voiceBox, selectionBox;
  juce::TextButton playButton, soloButton, saveButton, loadButton;
  juce::Label seedLabel, keyLabel, readout;
  juce::TextEditor seedEditor, keyEditor;
  juce::Viewport viewport;
  juce::Component rows;
  std::vector<std::unique_ptr<KnobRow>> rowWidgets;
  juce::Label trimLabels[BotBand::kNumVoices];
  juce::Slider trimSliders[BotBand::kNumVoices];
  std::unique_ptr<juce::FileChooser> chooser;
};

// ---------------------------------------------------------------------------

class BandLabApplication : public juce::JUCEApplication {
public:
  const juce::String getApplicationName() override { return "AntiphonBandLab"; }
  const juce::String getApplicationVersion() override { return "0.1"; }

  void initialise(const juce::String &) override {
    window = std::make_unique<Window>();
  }

  void shutdown() override { window = nullptr; }

private:
  class Window : public juce::DocumentWindow {
  public:
    Window()
        : juce::DocumentWindow("Antiphon Band Lab", juce::Colour(0xff1b1f24),
                               juce::DocumentWindow::allButtons) {
      setUsingNativeTitleBar(true);
      setContentOwned(new BandLabComponent(), true);
      setResizable(true, false);
      centreWithSize(1000, 700);
      setVisible(true);
    }

    void closeButtonPressed() override {
      juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }
  };

  std::unique_ptr<Window> window;
};

} // namespace

START_JUCE_APPLICATION(BandLabApplication)
