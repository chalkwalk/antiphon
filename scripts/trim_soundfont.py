#!/usr/bin/env python3
"""Keep a handful of presets from a SoundFont and drop the rest.

A packaging tool, not part of the plugin. It exists because a General MIDI bank
is 128 instruments and Antiphon wants perhaps twenty: the case for sampled
voices at all is narrow (`ROADMAP.md`), covering only what a physical model will
never do well -- an acoustic piano, a brass section, bowed strings, reeds.
Everything the band already plays sounds better modelled, and nobody needs the
helicopter.

WHAT THIS SAVES, MEASURED, because the obvious guess is wrong. Dropping 92% of
GeneralUser GS's presets removes only 59% of its bytes. The sound effects are
cheap -- a fraction of a second each -- and the expensive presets are exactly
the ones worth keeping, because a convincing piano or string section is many
megabytes of multisampling. Trimming to a quarter of the presets gives about
40% of the size, not 25%.

It compounds with SF3 though, and that is where it pays: 41% of the samples at
Ogg quality 0.8 (33%) is about 4 MB, from 31.

  python3 scripts/trim_soundfont.py in.sf2 out.sf2 --preset 0:0 --preset 0:48
  python3 scripts/trim_soundfont.py in.sf2 out.sf2 --set core
  sf3convert -q 0.8 out.sf2 out.sf3

A SoundFont is a RIFF file whose `pdta` list is five parallel arrays chained by
index -- presets point into bags, bags into generators, generators at
instruments, instruments into their own bags and generators, and those at
samples. Removing anything means renumbering every chain that follows it, which
is the whole of the work here. The sample data itself is copied verbatim, so
this is lossless: it only ever removes.
"""

import argparse
import struct
import sys

# Generator operators we have to follow (SF2 spec section 8.1).
GEN_INSTRUMENT = 41
GEN_SAMPLE_ID = 53

# The spec requires at least 46 zero sample-frames between samples so that an
# interpolating synth reading past a loop point cannot walk into its neighbour.
SAMPLE_PADDING = 46

# What Antiphon would actually use. Bank 0 programs, General MIDI numbering.
SETS = {
    # One of each family, for a first experiment.
    "minimal": [0, 24, 40, 48, 56, 65, 71, 73],

    # Everything a physical model will not do well, and nothing else.
    "core": [0, 11, 12, 16, 19, 24, 25, 40, 42, 45, 48, 52,
             56, 57, 58, 60, 61, 64, 65, 66, 68, 71, 73],

    # `core` plus the instruments the band itself plays: five basses, the
    # electric guitars, both electric pianos, harpsichord and clavinet.
    #
    # These overlap what the synthesis already does, and that is the point --
    # having both is how you find out which is better, and the answer is
    # unlikely to be the same for a fingered bass as for a Rhodes.
    "band": [0, 4, 5, 6, 7, 11, 12, 16, 19, 24, 25, 26, 27, 28, 29, 30,
             32, 33, 34, 35, 36, 37, 40, 42, 45, 48, 52,
             56, 57, 58, 60, 61, 64, 65, 66, 68, 71, 73],

    # Everything except the synthesisers (80-103), the sound effects (120-127)
    # and the two synth basses.
    #
    # Worth knowing before choosing it: this is the set most people would name
    # first, and it saves the LEAST. Synths and effects together are 11% of the
    # bytes, because they are short and thin. All the weight is in the acoustic
    # multisampling, which is exactly what any of these sets is keeping.
    "acoustic": ([p for p in range(0, 38)] + [p for p in range(40, 80)]
                 + [p for p in range(104, 120)]),
}

# Drum kits live in bank 128, and they are the bargain in this file.
#
# Each is about 2.7 MB on its own, but they SHARE almost everything -- the GS
# kits are largely one set of samples remapped, with a handful of kit-specific
# pieces. So the first costs 2.69 MB and the other seven cost 1.38 MB between
# them. Eight kits for barely more than one.
#
# Worth taking whole for a second reason. The modelled kit has three pieces --
# kick, snare, hat -- and this is 65 samples per kit including five toms, ride,
# ride bell, crash, splash, china, cowbell, tambourine, claves, congas, bongos,
# timbales, agogo, guiro, cabasa, shaker, whistle and woodblock. None of that is
# a physical model we are ever going to write, and percussion one-shots are also
# the best case for Ogg compression, since nothing is looped.
# The acoustic ones. Electronic and 808/909 are dropped because the modelled kit
# is already a synthesised one and does that job better -- it varies with
# velocity and never repeats, which is what a drum machine sample cannot do.
# Room is dropped because the kit is put in a room of our own (BotDsp::Room), and
# baking a second one into the samples would be two rooms.
DRUM_KITS = [0, 16, 32, 40, 48]  # Standard, Power, Jazz, Brush, Orchestral

# All of them, including the electronic kits, for comparison.
DRUM_KITS_ALL = [0, 8, 16, 24, 25, 32, 40, 48]


def chunks(buf, start, end):
    i = start
    while i + 8 <= end:
        cid = buf[i:i + 4].decode("latin1")
        size = struct.unpack("<I", buf[i + 4:i + 8])[0]
        yield cid, i + 8, size
        i += 8 + size + (size & 1)


class SoundFont:
    def __init__(self, path):
        self.raw = open(path, "rb").read()
        if self.raw[0:4] != b"RIFF" or self.raw[8:12] != b"sfbk":
            raise SystemExit(f"{path} is not a SoundFont")

        total = struct.unpack("<I", self.raw[4:8])[0]
        self.lists = {}
        for cid, off, size in chunks(self.raw, 12, 8 + total):
            if cid == "LIST":
                name = self.raw[off:off + 4].decode("latin1")
                self.lists[name] = (off + 4, off + size)

        self.info = {}
        for cid, off, size in chunks(self.raw, *self.lists["INFO"]):
            self.info[cid] = self.raw[off:off + size]

        self.sdta = {}
        for cid, off, size in chunks(self.raw, *self.lists["sdta"]):
            self.sdta[cid] = (off, size)

        pdta = {}
        for cid, off, size in chunks(self.raw, *self.lists["pdta"]):
            pdta[cid] = (off, size)

        def records(name, width):
            off, size = pdta[name]
            return [self.raw[off + i * width:off + (i + 1) * width]
                    for i in range(size // width)]

        self.phdr = records("phdr", 38)
        self.pbag = records("pbag", 4)
        self.pmod = records("pmod", 10)
        self.pgen = records("pgen", 4)
        self.inst = records("inst", 22)
        self.ibag = records("ibag", 4)
        self.imod = records("imod", 10)
        self.igen = records("igen", 4)
        self.shdr = records("shdr", 46)

    # -- reading the chains --------------------------------------------------

    @staticmethod
    def _u16(rec, at):
        return struct.unpack("<H", rec[at:at + 2])[0]

    @staticmethod
    def name_of(rec):
        return rec[:20].split(b"\0")[0].decode("latin1")

    def preset_key(self, i):
        return (self._u16(self.phdr[i], 22), self._u16(self.phdr[i], 20))

    def preset_bags(self, i):
        return range(self._u16(self.phdr[i], 24), self._u16(self.phdr[i + 1], 24))

    def instrument_bags(self, i):
        return range(self._u16(self.inst[i], 20), self._u16(self.inst[i + 1], 20))

    def _gen_span(self, bags, bag, gens):
        start = self._u16(bags[bag], 0)
        end = self._u16(bags[bag + 1], 0) if bag + 1 < len(bags) else len(gens)
        return range(start, end)

    def instruments_used_by(self, preset):
        used = set()
        for b in self.preset_bags(preset):
            for g in self._gen_span(self.pbag, b, self.pgen):
                if self._u16(self.pgen[g], 0) == GEN_INSTRUMENT:
                    used.add(self._u16(self.pgen[g], 2))
        return used

    def samples_used_by(self, instrument):
        used = set()
        if instrument >= len(self.inst) - 1:
            return used
        for b in self.instrument_bags(instrument):
            for g in self._gen_span(self.ibag, b, self.igen):
                if self._u16(self.igen[g], 0) == GEN_SAMPLE_ID:
                    used.add(self._u16(self.igen[g], 2))
        return used

    def sample_span(self, s):
        return struct.unpack("<II", self.shdr[s][20:28])


def build(sf, keep_keys):
    """Return the bytes of a SoundFont holding only the wanted presets."""
    keep_presets = [i for i in range(len(sf.phdr) - 1)
                    if sf.preset_key(i) in keep_keys]
    if not keep_presets:
        raise SystemExit("no presets matched")

    # Follow the chains outward to everything still referenced.
    keep_instruments = sorted({i for p in keep_presets
                               for i in sf.instruments_used_by(p)})
    keep_samples = sorted({s for i in keep_instruments
                           for s in sf.samples_used_by(i)})
    inst_index = {old: new for new, old in enumerate(keep_instruments)}
    smpl_index = {old: new for new, old in enumerate(keep_samples)}

    # -- sample data, repacked with the required silence between ------------
    smpl_off, _ = sf.sdta["smpl"]
    out_samples = bytearray()
    new_span = {}
    for s in keep_samples:
        start, end = sf.sample_span(s)
        first = len(out_samples) // 2
        out_samples += sf.raw[smpl_off + start * 2:smpl_off + end * 2]
        new_span[s] = (first, len(out_samples) // 2)
        out_samples += b"\0" * (SAMPLE_PADDING * 2)

    # -- instrument level ---------------------------------------------------
    new_ibag, new_igen, new_imod = [], [], []
    new_inst = []
    for old in keep_instruments:
        new_inst.append(sf.inst[old][:20] + struct.pack("<H", len(new_ibag)))
        for b in sf.instrument_bags(old):
            gen_start = len(new_igen)
            mod_start = len(new_imod)
            for g in sf._gen_span(sf.ibag, b, sf.igen):
                op = sf._u16(sf.igen[g], 0)
                if op == GEN_SAMPLE_ID:
                    old_id = sf._u16(sf.igen[g], 2)
                    if old_id not in smpl_index:
                        continue
                    new_igen.append(struct.pack("<HH", op, smpl_index[old_id]))
                else:
                    new_igen.append(sf.igen[g])
            m0 = sf._u16(sf.ibag[b], 2)
            m1 = (sf._u16(sf.ibag[b + 1], 2)
                  if b + 1 < len(sf.ibag) else len(sf.imod))
            for m in range(m0, m1):
                new_imod.append(sf.imod[m])
            new_ibag.append(struct.pack("<HH", gen_start, mod_start))
    new_inst.append(b"EOI" + b"\0" * 17 + struct.pack("<H", len(new_ibag)))
    new_ibag.append(struct.pack("<HH", len(new_igen), len(new_imod)))
    new_igen.append(b"\0" * 4)
    new_imod.append(b"\0" * 10)

    # -- preset level -------------------------------------------------------
    new_pbag, new_pgen, new_pmod = [], [], []
    new_phdr = []
    for old in keep_presets:
        new_phdr.append(sf.phdr[old][:24] + struct.pack("<H", len(new_pbag))
                        + sf.phdr[old][26:])
        for b in sf.preset_bags(old):
            gen_start = len(new_pgen)
            mod_start = len(new_pmod)
            for g in sf._gen_span(sf.pbag, b, sf.pgen):
                op = sf._u16(sf.pgen[g], 0)
                if op == GEN_INSTRUMENT:
                    old_id = sf._u16(sf.pgen[g], 2)
                    if old_id not in inst_index:
                        continue
                    new_pgen.append(struct.pack("<HH", op, inst_index[old_id]))
                else:
                    new_pgen.append(sf.pgen[g])
            m0 = sf._u16(sf.pbag[b], 2)
            m1 = (sf._u16(sf.pbag[b + 1], 2)
                  if b + 1 < len(sf.pbag) else len(sf.pmod))
            for m in range(m0, m1):
                new_pmod.append(sf.pmod[m])
            new_pbag.append(struct.pack("<HH", gen_start, mod_start))
    new_phdr.append(b"EOP" + b"\0" * 17 + struct.pack("<HH", 0, 0)
                    + struct.pack("<H", len(new_pbag)) + b"\0" * 12)
    new_pbag.append(struct.pack("<HH", len(new_pgen), len(new_pmod)))
    new_pgen.append(b"\0" * 4)
    new_pmod.append(b"\0" * 10)

    # -- sample headers, pointing into the repacked data --------------------
    new_shdr = []
    for old in keep_samples:
        rec = bytearray(sf.shdr[old])
        start, end = sf.sample_span(old)
        first, last = new_span[old]
        loop_start, loop_end = struct.unpack("<II", rec[28:36])
        struct.pack_into("<IIII", rec, 20, first, last,
                         first + (loop_start - start), first + (loop_end - start))
        # A link to a sample we dropped would dangle; make it mono instead.
        link = struct.unpack("<H", rec[42:44])[0]
        if link not in smpl_index:
            struct.pack_into("<HH", rec, 42, 0, 1)  # monoSample
        else:
            struct.pack_into("<H", rec, 42, smpl_index[link])
        new_shdr.append(bytes(rec))
    new_shdr.append(b"EOS" + b"\0" * 43)

    # -- write it out -------------------------------------------------------
    def chunk(cid, payload):
        body = payload + (b"\0" if len(payload) & 1 else b"")
        return cid.encode("latin1") + struct.pack("<I", len(payload)) + body

    info = b"INFO"
    for cid in ("ifil", "isng", "INAM", "irom", "iver", "ICRD", "IENG",
                "IPRD", "ICOP", "ICMT", "ISFT"):
        if cid in sf.info:
            info += chunk(cid, sf.info[cid])

    sdta = b"sdta" + chunk("smpl", bytes(out_samples))

    pdta = b"pdta"
    for cid, recs in (("phdr", new_phdr), ("pbag", new_pbag), ("pmod", new_pmod),
                      ("pgen", new_pgen), ("inst", new_inst), ("ibag", new_ibag),
                      ("imod", new_imod), ("igen", new_igen), ("shdr", new_shdr)):
        pdta += chunk(cid, b"".join(recs))

    body = b"sfbk" + chunk("LIST", info) + chunk("LIST", sdta) + chunk("LIST", pdta)
    return b"RIFF" + struct.pack("<I", len(body)) + body, keep_presets, keep_samples


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("input")
    ap.add_argument("output")
    ap.add_argument("--preset", action="append", default=[],
                    metavar="BANK:PROGRAM", help="keep this preset; repeatable")
    ap.add_argument("--set", choices=sorted(SETS),
                    help="keep a named set of bank 0 programs")
    ap.add_argument("--drums", action="store_true",
                    help="keep the five acoustic drum kits in bank 128")
    ap.add_argument("--all-drums", action="store_true",
                    help="keep all eight, including the electronic kits")
    ap.add_argument("--list", action="store_true",
                    help="print the presets in the input and stop")
    args = ap.parse_args()

    sf = SoundFont(args.input)

    if args.list:
        for i in range(len(sf.phdr) - 1):
            bank, prog = sf.preset_key(i)
            print(f"{bank:3d}:{prog:3d}  {sf.name_of(sf.phdr[i])}")
        return

    keep = set()
    for spec in args.preset:
        bank, _, prog = spec.partition(":")
        keep.add((int(bank), int(prog)))
    if args.set:
        keep |= {(0, p) for p in SETS[args.set]}
    if args.drums:
        keep |= {(128, p) for p in DRUM_KITS}
    if args.all_drums:
        keep |= {(128, p) for p in DRUM_KITS_ALL}
    if not keep:
        raise SystemExit("nothing to keep: pass --preset or --set")

    data, presets, samples = build(sf, keep)
    open(args.output, "wb").write(data)

    before = len(sf.raw) / 1024 / 1024
    after = len(data) / 1024 / 1024
    print(f"{len(presets)} presets, {len(samples)} samples")
    print(f"{before:.2f} MB -> {after:.2f} MB  ({100 * after / before:.1f}%)")


if __name__ == "__main__":
    main()
