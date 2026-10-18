#!/usr/bin/env python3
"""Builds the Turkish Wordle word lists (5-letter root words) from licensed sources.

  guesses.txt  every 5-letter lemma of the Zemberek-NLP lexicon (Apache-2.0; TDK-based lemmas, no proper
               names or abbreviations) plus 5-letter verb roots (infinitive without -mek/-mak)
  answers.txt  the common subset: non-verb lemmas that are not plural (-ler/-lar), not pronouns, conjunctions,
               interjections, postpositions, determiners or question words, and that occur in the first 25 000
               words of hermitdave/FrequencyWords tr_50k (CC-BY-SA-4.0)

Circumflexes are folded (â→a, î→i, û→u). Usage: tools/wordle/build-words.py [outdir] (needs network once).
"""
import os, sys, urllib.request

ZEMBEREK = "https://raw.githubusercontent.com/ahmetaa/zemberek-nlp/037202c7e18b977c95181a76db05cb1e95846554/morphology/src/main/resources/tr/"
FREQ = "https://raw.githubusercontent.com/hermitdave/FrequencyWords/525f9b560de45753a5ea01069454e72e9aa541c6/content/2018/tr/tr_50k.txt"
TR = set("abcçdefgğhıijklmnoöprsştuüvyz")
NOT_ANSWER = ("P:Pron", "P:Conj", "P:Interj", "P:Postp", "P:Det", "P:Ques", "P:Dup", "P:Num")
COMMON = 25000
# never picked as the answer (still valid guesses): particles/forms that are not root words, and insults or slurs
EXCLUDE_ANSWER = {"değil", "demek", "olmaz", "şurda", "burda", "orda", "zenci", "enayi", "aptal", "salak", "ahmak",
                  "kahpe", "lanet", "budal", "gavur", "keriz", "dinsiz", "imansız"}


def fetch(url):
    with urllib.request.urlopen(url, timeout=60) as r:
        return r.read().decode("utf-8")


def norm(w):
    w = w.replace("I", "ı").replace("İ", "i").lower()
    return w.replace("â", "a").replace("î", "i").replace("û", "u")


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "apps/wordle/words"
    guesses, answer_ok = set(), set()
    for name in ("master-dictionary.dict", "non-tdk.dict"):
        for line in fetch(ZEMBEREK + name).splitlines():
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            word, _, tag = line.partition(" [")
            word = word.strip()
            if " " in word or "P:Punc" in tag or "Abbrv" in tag or "Prop" in tag:
                continue
            w = norm(word)
            if "P:" not in tag and (w.endswith("mek") or w.endswith("mak")):
                root = w[:-3]  # verb root: valid guess, never an answer
                if len(root) == 5 and set(root) <= TR:
                    guesses.add(root)
                continue
            if len(w) != 5 or not set(w) <= TR:
                continue
            guesses.add(w)
            if not any(t in tag for t in NOT_ANSWER) and not w.endswith(("ler", "lar")):
                answer_ok.add(w)
    rank = {}
    for i, line in enumerate(fetch(FREQ).splitlines()):
        if line.strip():
            rank.setdefault(norm(line.split()[0]), i)
    answers = sorted(w for w in answer_ok if rank.get(w, COMMON) < COMMON and w not in EXCLUDE_ANSWER)
    os.makedirs(out, exist_ok=True)
    # sorted by byte value: the app binary-searches with strcmp
    for fname, words in (("guesses.txt", guesses), ("answers.txt", answers)):
        data = sorted(words, key=lambda s: s.encode("utf-8"))
        with open(os.path.join(out, fname), "w", encoding="utf-8") as f:
            f.write("\n".join(data) + "\n")
    print(f"{len(guesses)} guesses, {len(answers)} answers -> {out}")


if __name__ == "__main__":
    main()
