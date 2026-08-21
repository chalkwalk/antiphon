#pragma once

#include <chalkwalk/dsp/Measure.h>

// Peak, rms, crest, dB, brightness, pitch and loudness live in
// `chalkwalk::dsp::measure` now. Nothing about them was ever specific to a
// Ninjam client: they are how you find out what a signal is, and the reason
// they had to move is that the ecosystem had grown three copies of `peak` and
// `rms` and two of `fundamentalHz` -- two pitch detectors, which is two
// answers to one question and no way to tell which is lying.
//
// An alias rather than a re-export list, because like `Harmony` and unlike
// `MusicalKey` there is nothing of Antiphon's to add: the whole of it moved.
// The old spelling is kept because the call sites read better for it -- what
// this repository measures is audio, and the library it comes from is dsp.
namespace AudioMeasure = chalkwalk::dsp::measure;
