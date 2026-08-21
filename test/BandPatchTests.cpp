#include "../src/jambot/BandPatch.h"
#include <JuceHeader.h>

// The parameter layer, which is what the band lab edits and what a tuning
// session hands back.
//
// Exact tests throughout: this is bookkeeping rather than sound, so there is no
// excuse for a statistical assertion here. The thing being defended is that a
// number a person listened to and settled on arrives back in the code as the
// same number, in the right instrument.

class BandPatchTests : public juce::UnitTest {
public:
  BandPatchTests() : juce::UnitTest("BandPatch", "music") {}

  void runTest() override {
    runKnobTests();
    runFileTests();
  }

  void runKnobTests() {
    beginTest("a range can say where its middle SOUNDS");
    {
      // Without this a seed draws uniformly in arithmetic, which for anything
      // perceptual is lopsided in tone: half of a 200..6000 Hz cutoff range
      // sits above 3100 Hz, where little audible is still changing, so a
      // "random" patch is bright four times out of five.
      BotVoice::Range r{200.0, 6000.0};
      expect(!r.centreSet(), "a range starts with nobody having listened");
      expectWithinAbsoluteError(r.mid(), 3100.0, 1e-9);
      expectWithinAbsoluteError(r.at(0.5), 3100.0, 1e-9);

      r.centre = 800.0; // where it actually sounds halfway
      expect(r.centreSet());

      // Exact at all three anchors, which is the property that makes it
      // possible to set by ear: you hear the value you pinned, not a curve's
      // idea of it.
      expectWithinAbsoluteError(r.at(0.0), 200.0, 1e-9);
      expectWithinAbsoluteError(r.at(0.5), 800.0, 1e-9);
      expectWithinAbsoluteError(r.at(1.0), 6000.0, 1e-9);

      // Monotonic, so a bigger draw is never a smaller value.
      double last = -1e18;
      for (int i = 0; i <= 100; ++i) {
        const double v = r.at(i / 100.0);
        expect(v >= last, "at() went backwards at u=" + juce::String(i / 100.0));
        expect(v >= r.lo && v <= r.hi, "at() left the range");
        last = v;
      }

      // Half the draws now land below the sonic centre rather than below the
      // arithmetic one, which is the entire point.
      int below = 0;
      for (int i = 0; i < 1000; ++i)
        if (r.at(i / 999.0) < 800.0)
          ++below;
      expect(below > 450 && below < 550,
             "draws below the centre: " + juce::String(below) + " of 1000");

      // And the inverse puts a fader back where a value came from.
      for (double u : {0.0, 0.25, 0.5, 0.75, 1.0})
        expectWithinAbsoluteError(r.positionOf(r.at(u)), u, 1e-9);
    }

    beginTest("every voice with knobs reports them, bound to live storage");
    {
      auto band = BandPatch::defaults();

      for (auto voice : {BotBand::Voice::Bass, BotBand::Voice::Keys,
                         BotBand::Voice::Lead}) {
        const auto knobs = BandPatch::knobsFor(band, voice);
        expect(knobs.size() >= 10,
               juce::String(BotBand::voiceName(voice)) + " reported only " +
                   juce::String((int)knobs.size()) + " knobs");

        for (const auto &knob : knobs) {
          expect(knob.value != nullptr && knob.range != nullptr,
                 "a knob was not bound");
          expect(knob.range->hi > knob.range->lo,
                 juce::String(knob.name) + " has an empty range");

          // The value must be a live reference into the patch, not a copy: the
          // whole design rests on a slider writing straight through to the
          // thing the renderer reads.
          const double was = *knob.value;
          *knob.value = was + 1.0;
          const auto again = BandPatch::knobsFor(band, voice);
          bool seen = false;
          for (const auto &other : again)
            if (other.name == knob.name) {
              expectWithinAbsoluteError(*other.value, was + 1.0, 1.0e-12,
                                        juce::String(knob.name) +
                                            " did not write through");
              seen = true;
            }
          expect(seen, juce::String(knob.name) + " went missing");
          *knob.value = was;
        }
      }
    }

    beginTest("the defaults start in the middle of every range");
    {
      // What a person tuning wants in front of them, and what makes the lab's
      // starting position reproducible where a seeded draw would not be.
      auto band = BandPatch::defaults();
      for (int c = 0; c < BandPatch::Band::kSelections; ++c) {
        band.keysCharacter = (BotVoice::PadCharacter)c;
        for (const auto &knob : BandPatch::knobsFor(band, BotBand::Voice::Keys))
          expectWithinAbsoluteError(*knob.value, knob.range->mid(), 1.0e-9,
                                    juce::String(knob.name) + " on " +
                                        BotVoice::padCharacterName(
                                            (BotVoice::PadCharacter)c));
      }
    }

    beginTest("each selection has its own storage");
    {
      // The bug this is here to catch: one patch shared between three
      // characters, so a session spent on brass is lost the moment you look at
      // strings, and a saved file applies every character's numbers to the same
      // place with the last one winning.
      auto band = BandPatch::defaults();

      band.keysCharacter = BotVoice::PadCharacter::Brass;
      *BandPatch::knobsFor(band, BotBand::Voice::Keys)[0].value = 11.5;

      band.keysCharacter = BotVoice::PadCharacter::Strings;
      const double strings =
          *BandPatch::knobsFor(band, BotBand::Voice::Keys)[0].value;
      expect(std::abs(strings - 11.5) > 1.0e-6,
             "editing the brass patch changed the strings patch");

      band.keysCharacter = BotVoice::PadCharacter::Brass;
      expectWithinAbsoluteError(
          *BandPatch::knobsFor(band, BotBand::Voice::Keys)[0].value, 11.5,
          1.0e-9, "the brass edit did not survive a look at strings");
    }

    beginTest("the ranges are the ones the seed draws from");
    {
      // The claim that makes the lab worth building: a slider's limits and the
      // sweet spot a seed picks inside are the same numbers, from one table. If
      // these ever diverge, tuning against the lab tunes something the room
      // will never play.
      auto band = BandPatch::defaults();

      for (int c = 0; c < BandPatch::Band::kSelections; ++c) {
        const auto character = (BotVoice::PadCharacter)c;
        band.keysCharacter = character;
        const auto knobs = BandPatch::knobsFor(band, BotBand::Voice::Keys);

        // 200 seeds, keeping only the patches of this character, and every
        // drawn value must land inside the slider's travel.
        int checked = 0;
        for (std::uint32_t seed = 1; seed <= 600 && checked < 40; ++seed) {
          const auto drawn = BotVoice::padPatchFor(seed * 2654435761u);
          if (drawn.character != character)
            continue;
          ++checked;

          BandPatch::Band probe = BandPatch::defaults();
          probe.keysCharacter = character;
          probe.keys[(int)character] = drawn;

          const auto drawnKnobs =
              BandPatch::knobsFor(probe, BotBand::Voice::Keys);
          for (size_t i = 0; i < drawnKnobs.size(); ++i)
            expect(*drawnKnobs[i].value >= knobs[i].range->lo - 1.0e-9 &&
                       *drawnKnobs[i].value <= knobs[i].range->hi + 1.0e-9,
                   juce::String(BotVoice::padCharacterName(character)) + " " +
                       juce::String(drawnKnobs[i].name) + " drew " +
                       juce::String(*drawnKnobs[i].value, 4) + " outside " +
                       juce::String(knobs[i].range->lo, 4) + ".." +
                       juce::String(knobs[i].range->hi, 4));
        }
        expect(checked > 10, "too few patches of this character to check");
      }
    }
  }

  void runFileTests() {
    beginTest("a band survives a round trip through a file");
    {
      auto band = BandPatch::defaults();

      // Move something in every selection of every voice, so a writer that
      // only saved the visible one is caught.
      double expected[3][3] = {};
      for (int c = 0; c < BandPatch::Band::kSelections; ++c) {
        band.keysCharacter = (BotVoice::PadCharacter)c;
        band.bassTechnique = (BotVoice::BassTechnique)c;
        band.lead.instrument = (BotVoice::LeadInstrument)c;

        int v = 0;
        for (auto voice : {BotBand::Voice::Keys, BotBand::Voice::Bass,
                           BotBand::Voice::Lead}) {
          auto knobs = BandPatch::knobsFor(band, voice);
          const double value = knobs[1].range->lo + 0.123;
          *knobs[1].value = value;
          knobs[1].range->hi = knobs[1].range->hi + 7.5;
          expected[c][v++] = value;
        }
      }
      band.trim[0] = 1.234;
      band.trim[3] = 0.876;

      const auto text = BandPatch::write(band);

      auto restored = BandPatch::defaults();
      std::string error;
      expect(BandPatch::read(text, restored, error), error);

      for (int c = 0; c < BandPatch::Band::kSelections; ++c) {
        restored.keysCharacter = (BotVoice::PadCharacter)c;
        restored.bassTechnique = (BotVoice::BassTechnique)c;
        restored.lead.instrument = (BotVoice::LeadInstrument)c;

        int v = 0;
        for (auto voice : {BotBand::Voice::Keys, BotBand::Voice::Bass,
                           BotBand::Voice::Lead}) {
          const auto knobs = BandPatch::knobsFor(restored, voice);
          expectWithinAbsoluteError(*knobs[1].value, expected[c][v], 1.0e-5,
                                    juce::String(BotBand::voiceName(voice)) +
                                        " selection " + juce::String(c));
          ++v;
        }
      }

      expectWithinAbsoluteError(restored.trim[0], 1.234, 1.0e-9);
      expectWithinAbsoluteError(restored.trim[3], 0.876, 1.0e-9);
    }

    beginTest("the ranges travel too, because they are the point");
    {
      // A tuning session settles two things and the second is the one that
      // cannot be recovered from the code: not "the cutoff should be 9" but
      // "the cutoff should be somewhere between 7 and 11". A file that carried
      // only values would throw that away.
      auto band = BandPatch::defaults();
      band.keysCharacter = BotVoice::PadCharacter::Poly;
      {
        auto knobs = BandPatch::knobsFor(band, BotBand::Voice::Keys);
        knobs[0].range->lo = 2.5;
        knobs[0].range->hi = 3.5;
        *knobs[0].value = 3.0;
      }

      auto restored = BandPatch::defaults();
      std::string error;
      expect(BandPatch::read(BandPatch::write(band), restored, error), error);

      restored.keysCharacter = BotVoice::PadCharacter::Poly;
      const auto knobs = BandPatch::knobsFor(restored, BotBand::Voice::Keys);
      expectWithinAbsoluteError(knobs[0].range->lo, 2.5, 1.0e-9);
      expectWithinAbsoluteError(knobs[0].range->hi, 3.5, 1.0e-9);
    }

    beginTest("a file a person edited still reads");
    {
      // Comments, blank lines, reordering, and a subset. Every one of these is
      // something somebody will do to a text file, and a format that breaks on
      // any of them is one nobody trusts enough to edit.
      const std::string text =
          "# my notes\n"
          "\n"
          "trim.Keys  0.44\n"
          "   \n"
          "Bass.picked.decaySeconds  1.9   0.5  3.0   # shorter\n"
          "Keys.brass.resonance  1.21\n";

      auto band = BandPatch::defaults();
      std::string error;
      expect(BandPatch::read(text, band, error), error);

      expectWithinAbsoluteError(band.trim[(int)BotBand::Voice::Keys], 0.44,
                                1.0e-9);

      band.bassTechnique = BotVoice::BassTechnique::Picked;
      expectWithinAbsoluteError(band.bassPatch().decaySeconds, 1.9, 1.0e-9);

      band.keysCharacter = BotVoice::PadCharacter::Brass;
      expectWithinAbsoluteError(band.keysPatch().resonance, 1.21, 1.0e-9);

      // A value with no range leaves the range alone rather than zeroing it.
      expect(band.keysRanges[(int)BotVoice::PadCharacter::Brass].resonance.hi >
                 band.keysRanges[(int)BotVoice::PadCharacter::Brass].resonance.lo,
             "a line without a range destroyed one");
    }

    beginTest("a file that says nothing says so");
    {
      // Silence is the dangerous failure here: a typo that leaves a session's
      // work unapplied, with the lab cheerfully showing defaults.
      auto band = BandPatch::defaults();
      std::string error;

      expect(!BandPatch::read("# just a comment\n\n", band, error),
             "an empty file was accepted");
      expect(error.find("nothing") != std::string::npos, error);

      expect(!BandPatch::read("Keys.poly.notAKnob 1.0\n", band, error),
             "an unknown knob was accepted");
      expect(error.find("notAKnob") != std::string::npos, error);

      expect(!BandPatch::read("Keys.poly.resonance\n", band, error),
             "a knob with no value was accepted");
    }
  }
};

static BandPatchTests bandPatchTests;
