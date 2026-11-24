#!/usr/bin/env python3
"""Prepare ArduPilot parameter metadata strings for machine translation.

Reads any number of apm.pdef.xml files (all vehicles, master and stable
versions), collects every displayed English string (humanName, documentation,
enum value and bitmask labels), de-duplicates by (kind, text) and writes
batches grouped by parameter prefix, with the parameter name and title as
context. ArduPilot has no community translation, so there are no references.

Translations are keyed by the English text (context "$text/<kind>"), so one
translation applies to every vehicle and firmware version using that text.

usage: prepare.py OUTDIR PDEF.xml [PDEF.xml ...] [--batch-chars N]
Writes OUTDIR/items.json and OUTDIR/batch_NNN.jsonl (+ empty .ref.jsonl).
"""
import argparse
import json
import os
import xml.etree.ElementTree as ET


def squash(s):
    return " ".join((s or "").split())


def strings(path):
    """(kind, text, param, title) for every displayed string in a pdef file."""
    vehicle = os.path.basename(path).split("-")[1]
    for p in ET.parse(path).getroot().iter("param"):
        name = (p.get("name") or "").split(":")[-1]
        title = squash(p.get("humanName"))
        if title:
            yield "human", title, name, title, vehicle
        doc = squash(p.get("documentation"))
        if doc:
            yield "desc", doc, name, title, vehicle
        labels = [squash(e.text) for e in list(p.iter("value")) + list(p.iter("bit")) if e.text]
        for f in p.iter("field"):  # old pdef files: "0:Disabled,1:Enabled"
            if f.get("name") in ("Values", "Bitmask") and f.text:
                labels += [squash(x.split(":", 1)[1]) for x in f.text.split(",") if ":" in x]
        for l in labels:
            if l:
                yield "label", l, name, title, vehicle


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("outdir")
    ap.add_argument("pdef", nargs="+")
    ap.add_argument("--batch-chars", type=int, default=30000)
    a = ap.parse_args()

    items = {}
    for path in a.pdef:
        for kind, text, name, title, vehicle in strings(path):
            it = items.get((kind, text))
            if not it:
                if kind == "label":
                    ctx = f"label in {name} ({title})"
                elif kind == "human":
                    ctx = f"title of {name}"
                else:
                    ctx = f"description of {name} ({title})"
                it = items[(kind, text)] = {"id": len(items), "kind": kind, "text": text, "context": ctx,
                                            "keys": [f"$text/{kind}"], "prefix": name.split("_")[0],
                                            "vehicles": []}
            if vehicle not in it["vehicles"]:
                it["vehicles"].append(vehicle)

    os.makedirs(a.outdir, exist_ok=True)
    allitems = sorted(items.values(), key=lambda i: (i["prefix"], i["id"]))
    json.dump(allitems, open(os.path.join(a.outdir, "items.json"), "w"), ensure_ascii=False, indent=0)

    batches, cur, size = [], [], 0
    for it in allitems:
        n = len(it["text"]) + len(it["context"])
        if cur and size + n > a.batch_chars:
            batches.append(cur)
            cur, size = [], 0
        cur.append(it)
        size += n
    if cur:
        batches.append(cur)
    for i, b in enumerate(batches):
        base = os.path.join(a.outdir, f"batch_{i:03d}")
        with open(base + ".jsonl", "w") as f:
            for it in b:
                f.write(json.dumps({"id": it["id"], "kind": it["kind"], "context": it["context"],
                                    "text": it["text"]}, ensure_ascii=False) + "\n")
        open(base + ".ref.jsonl", "w").close()
    print(f"{len(allitems)} unique strings from {len(a.pdef)} files in {len(batches)} batches "
          f"of <= {a.batch_chars} chars")


if __name__ == "__main__":
    main()
