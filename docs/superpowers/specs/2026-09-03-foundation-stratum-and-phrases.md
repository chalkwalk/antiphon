# The foundation stratum, and phrases inside an interval

Design, 2026-09-03. Written before any of it is built.

**What this is for.** The band never repeats anything. Every figure spans a
whole interval, so nothing recurs inside one, and a long session meanders --
`ROADMAP.md`, *Form: repetition, tension and release*. This is the first two
steps of that branch, plus one prerequisite the roadmap did not know it needed.

**The prerequisite is the surprise.** The roadmap's middle item -- a phrase
shorter than the interval, repeated -- cannot be built for the bass as the code
stands, because the code contains an explicit and correct argument against it.
Fixing that is a structural change, and the structure it wants is one that
`libs/jambot/docs/BOT-CHAT.md` section 16 has already designed for another
reason entirely.

---

## 1. Why the bass cannot repeat today

`figureFor(Voice::Bass)` derives the bass from the kick by doubling -- twice
the steps, twice the pulses -- and then nudges the pulse count to the nearest
value **coprime with the steps**, so that the figure does not repeat inside the
interval. The comment says why, and it is right:

> exactly 2k shares a factor with 2s and so repeats inside the interval -- and
> a bass figure that repeats is doubling the kick again by another route. Twice
> four pulses over thirty-two steps has period four: `x...` eight times, a
> metronome.

So a short phrase period turns the bass back into a second kick drum. That is
not an obstacle to route around; it is the direct consequence of the bass's
figure being **computed by arithmetic from the kick** rather than **selected
against a shared part**. Coprimality is the only tool available for keeping the
two apart when one is a scaled copy of the other.

Under a stratum the bass is defined *against* the kick rather than derived from
it, and the collapse becomes impossible by construction rather than avoided by
a trick. That is the whole reason this spec starts somewhere the roadmap did
not.

---

## 2. What section 16.2 got wrong, and the amendment

`BOT-CHAT.md` 16.2 states one rule and cites one example:

> The second member of a stratum is always a subset or a complement of the
> first, never an independent idea. One rule, not eight special cases, and it
> is the rule the bass already follows against the kick.

**The bass does not follow that rule.** `renderBass` collects an onset when any
of three things is true -- a chord change, a kick onset, or its own figure --
which is a **union**, not a complement. The comment gives a reason that reads
as considered rather than accidental:

> Every kick gets a bass note, plus the figure's own. The union rather than the
> figure alone: locking to the kick has to mean actually landing on it, and the
> doubled Euclidean does not do that by itself.

A complement would put the bass only where the kick is not -- no bass note on
the downbeat -- which is close to the opposite of what a rhythm section does.

**Decided: the code is right and the vocabulary widens.** The rule worth
keeping is the intent, not the two words it was expressed in:

> A second member's figure is a function of the first's, never an independent
> idea.

with four admissible policies:

| policy | takes | example |
|---|---|---|
| `Whole` | the entire part | -- |
| `Subset` | a marked or chosen few | kit (the kick onsets) |
| `Union` | the first's onsets plus its own | bass |
| `Complement` | only where the first is not | perc (16.2 role 7) |

**Role 1 needs the same widening, for a different reason.** 16.2 has the kit
select "the whole figure". It cannot, once the part is at the bass's
resolution: the part then contains the bass's own onsets, and a kit playing
every one of them would be playing the bass line on a drum. The kit takes the
kick-marked `Subset`.

That is forced by section 4.2 rather than chosen. The phrase period applies to
the shared part, and the riff is mostly made of the bass's own onsets -- so a
part coarse enough for the kit to play whole is a part in which the riff does
not repeat. The resolution has to be fine and the kit's selection has to be
partial.

This is not a cosmetic change. The refactor's entire verification plan is
byte-identical output at N=4, and under "complement" that is unreachable --
it is a different part. The vocabulary has to be settled before any extraction,
which is why it is settled here.

`BOT-CHAT.md` 16.2 is amended to match, in the same change that builds this.

---

## 3. Layer one: the foundation stratum

**Audibly a no-op. Provably so.**

The stratum computes a **full part** for the interval on the fine grid --
`kick.steps * 2`, the bass's existing resolution:

- the union of the kick's onsets (mapped up to the fine grid) and the doubled
  Euclidean figure's;
- each onset carrying a strength;
- each onset tagged with whether it is a kick onset.

Members then select:

| member | policy | takes |
|---|---|---|
| kit | `Subset` | the kick-marked onsets, at the coarse grid, with `accents()` velocities as now |
| bass | `Union` | the whole part |

`renderBass` already computes exactly this union, so no arithmetic moves.

### 3.1 Scope: only the kick joins the shared part

The snare and the hats stay inside the kit's realisation. They are how a kit
plays the foundation, not the ground the bass relates to -- nothing outside
`renderDrums` refers to them, and `kitPattern` already holds them together.
Pulling them into the shared part would widen this landing for no consumer.

The keys, the lead and their strata are **out of scope entirely**. They arrive
with *A band of more than four*; this spec builds the one stratum that has a
second member today.

### 3.2 The harmonic overlay

16.2 says a full part carries "a figure of onsets with strengths, plus the
harmonic content where it is pitched". The chord-change onsets belong to that
harmonic half: the bass must land on a change, the kit has no opinion about
one, and an unpitched member simply ignores it. So `onChange` stays a rule of
the pitched member rather than becoming a rhythmic onset everybody sees.

### 3.3 How it is verified

The way the seed split was verified, which is the precedent this repository
already trusts: render intervals before and after and **compare byte for
byte**, across seeds, `bpi` and `bpm`. Not "the suite passes".

---

## 4. Layer two: the phrase, and `performanceSeed`

**These land together, and the roadmap is explicit about why:** a fresh jitter
per interval changes the audio for no benefit until something returns for it to
differentiate.

### 4.1 `performanceSeed`

The per-hit jitter, the per-note drift and the velocity variation all seed from
`saltedSeed(voice, seed) + step`, with **no interval term at all** -- so every
interval is jittered identically today, and what makes two consecutive drum
intervals differ is the hat rotation and nothing else.

`performanceSeed(voice, seed, intervalIndex)` replaces `saltedSeed` at those
sites, for **all four voices at once** -- snare, hat, fill-snare, bass string,
and the keys and lead equivalents. It is not a stratum-dependent change and
splitting it would leave half the band jittered identically every interval
while the other half was not, which is a worse state than either end of it.

The lead's note contour is already `Hold::Interval` and is untouched: it varies
per interval today, and it is a figure decision rather than a performance one.

This also strengthens the interlock the roadmap flags: `BotBandTests` asserts
two consecutive drum intervals are not bit-identical, and today only the hat
rotation carries that. Afterwards the performance carries it, which is what
makes genuine repetition safe later.

### 4.2 The phrase period

The full part gains a period and is **tiled** across the interval.

**The period comes from the chart where the chart has one to give.**
`Harmony::layoutChart` **stretches** a chart across exactly one interval and
never loops it, so "the chart's period" can only mean its *internal* repeat:
the smallest `p` with `chart[i] == chart[i mod p]` for every `i`. A repeat then
lands on the same chord it landed on last time, and the riff and the harmony
agree without either knowing about the other.

The seed picks a whole multiple of `p`, so `shake` reaches the shape of the
music and not only its notes.

**Where the chart has no internal repeat, the seed picks a fraction** -- `1/1`,
`1/2` or `1/4` of the interval. This is the common case, not the edge case:
`defaultDegreeLoop` is `I V vi IV` in major and `i VI III VII` in minor, four
bars with no repetition inside them, so a chart-only rule would leave the
default practice room exactly as it is.

Deciding to repeat across a change is deliberate, and the reason is musical
rather than a concession:

> A repeat in melodic terms can happen over different chords, often
> intentionally, as it recontextualises the element. As long as the repeated
> phrase is compatible enough with both chords under it, the repeat is
> acceptable.

Section 5 is what makes "compatible enough" a computation.

### 4.3 What the drums do, which is nothing new

The kit does **not** get a phrase to repeat, and the argument is exact.

The roadmap's "the form is illegible unless something marks it" rests on the
interval delay: across intervals the form you hear is rotated against the form
being played, so a boundary has to be announced. **Within** an interval there
is no rotation -- you hear it in order -- so a repeat is self-evident and needs
no marker. A drummer does not fill every two bars.

So the fill stays every fourth interval and becomes the section marker later,
under *Turnarounds mark the form*. The kit's only change here is that the
kick's period shortens from the whole interval to the phrase, which is
**consistent with the kick's own stated intent**:

> A short period is what makes a kick a pulse you can rely on; movement is what
> a bass wants and a kick does not.

The bass gets the riff. The kit's steady figure becomes the ground the riff is
heard against.

---

## 5. What repeats in pitch

A bass line wants to state the harmony. A literal repeat wants to ignore it.
Both survive, in three rules:

1. **The rhythm repeats always.** Free, and harmonically safe: every onset
   already takes its pitch from the chart, so a repeated rhythm adapts by
   construction.

2. **The pitch contour repeats literally where the spanned chords share enough
   admissible notes.** The pool is the intersection of the sounding sets of
   every chord the phrase spans. `chalkwalk::music` already has the parts --
   `SoundingChord` reduces a chord to pitch classes, and `rankCeiling` gates
   admissibility by metric strength -- and the lead already uses both.

3. **Chord-change onsets always re-root, overriding the phrase.** That rule
   exists today: `onChange` forces a note at velocity 1.0, because "a bass
   player lands on the change; leaving it to the rotation means the harmony is
   sometimes announced by nobody".

The riff repeats and its landing notes move under it, which is what a bass
player does over a progression.

---

## 6. What this does not do

- **No form across intervals.** AABA is the third item in the roadmap branch
  and stays there. This spec puts no two identical intervals next to each
  other, so `BotBandTests`' interlock holds on its own terms throughout.
- **No other strata.** Pad, lead and accent arrive with *A band of more than
  four*.
- **No deviation.** An occasional departure whose likelihood grows with the
  repeat count belongs to the figure seed and the repeat count, and wants a
  form to depart from.
- **No change to the default chart.** Considered and rejected: it lives in
  `chalkwalk-music` and reaches consumers that are not this band.

---

## 7. Open questions

1. **How many common tones is "enough"** before the pitch repeats literally
   rather than adapting per onset. This wants ears and `AntiphonVoiceLab`
   rather than a number chosen now -- the same way the lead's contour weight
   was measured at 2 rather than argued to.
2. **Whether the seed's multiple of `p` should be bounded by the interval.** A
   chart with `p` equal to the interval can only take multiple 1; a chart with
   `p` of two bars at `bpi` 16 could take 1 or 2. The bound is arithmetic; what
   is open is whether a phrase longer than the interval means anything before
   the form exists.
3. **Whether the fraction fallback should align to bars.** A `1/2` phrase in an
   interval whose chart has three bars does not land on a bar line, and it is
   not obvious whether that is a fault or a syncopation.

---

## 8. Order of work

1. Foundation stratum, byte-identical, proved by rendering. Amend
   `BOT-CHAT.md` 16.2 in the same change.
2. `performanceSeed` and the phrase period, together. Audible; judged in
   `AntiphonVoiceLab` before the thresholds in section 7 are fixed.

The form, deviation and the remaining strata follow in `ROADMAP.md`'s order and
are not part of this.
