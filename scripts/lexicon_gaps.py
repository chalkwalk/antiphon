#!/usr/bin/env python3
"""Propose lexicon entries for BotLanguage, from the corpus it already has.

The idea is Ellen Riloff's, from the information-extraction work of the
nineties (AutoSlog, AAAI-93; mutual bootstrapping, AAAI-99): you do not write a
domain dictionary by thinking hard, you let the text propose candidates from
their local context and have a person accept or reject them. AutoSlog's claim
was that this turned about 1500 hours of dictionary building into about 5 --
not because the machine was clever, but because reviewing a ranked list is a
different job from staring at a blank page.

The unsupervised form needs a body of raw in-domain text, and there is no
corpus of real Ninjam chat to point it at. But `bot-phrases.txt` is LABELLED,
which makes the same idea much easier: for every word the engine does not
recognise, count which intents its lines were supposed to resolve to. A word
that appears only in RESHUFFLE lines is a reshuffle word we have not written
down yet.

It proposes; it never edits. A wrong lexicon entry is the most expensive defect
this engine has -- it produces a confident wrong answer rather than an honest
fallback -- so every entry stays hand-written and reviewed.

What it found on its first run is the argument for it. It independently
recovered three rules that had been derived by hand and by eye (a trailing
"like" means the sound, a trailing "in" means the key, "running at" means the
tempo), which is the evidence that its signal is real; and it surfaced two gaps
that reading the failures could not, because they were not failing:

  - `try` appeared in five RESHUFFLE lines and was in no table. Every one of
    them passed anyway, carried by an `else` or an `again` sitting next to it.
    "try it" would have failed, and nothing in the corpus said so.
  - `length` likewise, carried each time by `interval`.

That is the class of defect this exists to find: not a miss, but a line that
passes for the wrong reason and will stop passing as soon as somebody phrases
it slightly differently.

Usage (from the repo root):
    python3 scripts/lexicon_gaps.py [--min-count N] [--min-purity 0.0-1.0]
"""

import argparse
import collections
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "src", "jambot", "BotLanguage.cpp")
CORPUS = os.path.join(ROOT, "test", "fixtures", "bot-phrases.txt")

# Words the engine deliberately drops. A gap report full of "the" is a gap
# report nobody reads.
TABLES = ("kLexicon", "kClassed", "kFiller", "kFillerUnlessAlone", "kCapability",
          "kQuestionWords", "kAuxiliaries", "kNegations", "kSecondPerson",
          "kFirstPerson", "kPossessive", "kDeterminer", "kSubject", "kModal",
          "kSmallTalk", "kExpansions")


def known():
    src = open(SRC, encoding="ascii").read()
    words = set()
    for name in TABLES:
        # `const Word kLexicon[]` and `const char *kFiller[]` both occur, and
        # the second has no space before the name.
        m = re.search(r"const [\w *]+?%s\[\] = \{(.*?)\};" % name, src, re.S)
        if not m:
            sys.exit("could not find %s in BotLanguage.cpp" % name)
        for token in re.findall(r'"([a-z\' ]+)"', m.group(1)):
            words.update(token.split())
    return words


def stem(word):
    """Shell out to the real stemmer rather than reimplement it.

    A second copy of the stemmer would drift from the first, and when it did,
    every difference would show up here as a fake gap -- which is exactly what
    happened on the first run, where a hand-rolled stemmer reported `timbre`,
    `sequence` and `figure` as missing when all three were already in the table
    under their stems.
    """
    return _stems()[word]


_cache = None


def _stems():
    global _cache
    if _cache is not None:
        return _cache
    words = sorted({w for _, line in corpus() for w in re.findall(r"[a-z']+", line)})
    prog = os.path.join(ROOT, "build", "botstem")
    source = prog + ".cpp"
    if not os.path.exists(prog) or os.path.getmtime(SRC) > os.path.getmtime(prog):
        os.makedirs(os.path.dirname(prog), exist_ok=True)
        open(source, "w").write(
            '#include "../src/jambot/BotLanguage.h"\n#include <iostream>\n'
            "int main(){std::string w;while(std::getline(std::cin,w))"
            "std::cout<<BotLanguage::stem(w)<<\"\\n\";}\n")
        # The shared libraries are on the include path because BotLanguage
        # reaches them through jambot/Music.h. An override is honoured for the
        # same reason the CMake build honours one -- so a library change can be
        # tried without a commit -- but the submodule is the default.
        includes = []
        for sub in ("music", "ninjam", "dsp"):
            root = (os.environ.get("CHALKWALK_%s_DIR" % sub.upper())
                    or os.path.join(ROOT, "libs", sub))
            includes += ["-I", os.path.join(root, "include")]
        subprocess.check_call(["g++", "-std=c++17", "-O1", "-o", prog, source, SRC]
                              + includes, cwd=ROOT)
    out = subprocess.run([prog], input="\n".join(words), capture_output=True,
                         text=True, check=True).stdout.split("\n")
    _cache = dict(zip(words, out))
    return _cache


def corpus():
    section = None
    for line in open(CORPUS, encoding="ascii"):
        text = line.split("#")[0].strip()
        if not text:
            continue
        if text.startswith("[") and text.endswith("]"):
            section = text[1:-1].strip()
            continue
        if section:
            yield section, text.lower()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--min-count", type=int, default=2)
    ap.add_argument("--min-purity", type=float, default=0.75)
    args = ap.parse_args()

    skip = known()
    where = collections.defaultdict(collections.Counter)
    for intent, line in corpus():
        for w in re.findall(r"[a-z']+", line):
            s = stem(w)
            if w in skip or s in skip:
                continue
            where[s][intent] += 1

    rows = []
    for word, counts in where.items():
        n = sum(counts.values())
        intent, top = counts.most_common(1)[0]
        purity = top / n
        if n >= args.min_count and purity >= args.min_purity:
            rows.append((purity, n, word, intent, counts))

    print("%-14s %5s %8s  %s" % ("word", "uses", "purity", "intents"))
    print("-" * 66)
    for purity, n, word, intent, counts in sorted(rows, key=lambda r: (-r[0], -r[1])):
        print("%-14s %5d %7.0f%%  %s" % (word, n, purity * 100,
              ", ".join("%s x%d" % (k, v) for k, v in counts.most_common(3))))
    print("\n%d candidates from %d unrecognised stems." % (len(rows), len(where)))
    print("A high-purity NONE candidate is working as intended: it is a word")
    print("the room uses that we are right not to answer.")


if __name__ == "__main__":
    main()
