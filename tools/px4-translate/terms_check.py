#!/usr/bin/env python3
"""Report terminology drift in machine translations (OUTDIR/batch_*.out.jsonl).
Each rule: (name, regex on English source or None, regex of a wrong Ukrainian variant)."""
import glob, json, os, re, sys

RULES = [
    ("swash plate -> автомат перекосу", r"swash", r"шайбов"),
    ("calibrated airspeed is not 'приладова'", r"calibrated airspeed", r"приладов"),
    ("throttle -> тяга, not дросель", r"throttle", r"дросел"),
    ("timeout -> тайм-аут", None, r"(?<![\w-])таймаут"),
    ("landing -> приземлення, not посадка", r"\bland(ing|ed|s)?\b", r"(?<!на )посадк(?!ов\w* смуг)"),
    ("arming -> взведення", r"\barm(ing|ed)?\b", r"охорон|активац"),
    ("failsafe -> аварійний режим", r"failsafe", r"режим\w* безпеки|відмовостійк"),
    ("range finder -> вимірювач дальності", r"range ?finder|range sensor|distance sensor", r"далекомір|дальномір|датчик\w* відстані"),
    ("system behaviour, not 'зачекайте'", None, r"[Зз]ачекайте"),
    ("attitude -> орієнтація", r"\battitude\b", r"просторов\w+ положенн|кутов\w+ положенн"),
    ("tiltrotor -> конвертоплан", r"tilt-?rotor", r"Tiltrotor|поворотн\w+ гвинт"),
    ("tailsitter -> тейлсітер", r"tail-?sitter", r"Tailsitter|тейлситер|[Тт]ейлсіттер"),
    ("quad-chute stays Latin", r"quad-?chute", r"квадчут"),
    ("Blimp stays Latin", r"\bblimp\b", r"[Бб]лімп"),
    ("EPM gripper is not ЕМІ", r"\b(emp|epm)\b", r"ЕМІ"),
    ("thrust with throttle -> сила тяги", r"thrust", r"тяг\w* при \w+ тяз\w* газу|тязі газу"),
]

def main(outdir):
    items = {i["id"]: i for i in json.load(open(os.path.join(outdir, "items.json")))}
    hits = {r[0]: [] for r in RULES}
    for of in sorted(glob.glob(os.path.join(outdir, "batch_*.out.jsonl"))):
        for line in open(of):
            if not line.strip():
                continue
            r = json.loads(line)
            src = items[r["id"]]["text"]
            for name, en, bad in RULES:
                if (en is None or re.search(en, src, re.I)) and re.search(bad, r["text"]):
                    hits[name].append((os.path.basename(of), r["id"]))
    for name, h in hits.items():
        print(f"{len(h):4d}  {name}" + (f"   e.g. {h[:4]}" if h else ""))

if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else ".")
