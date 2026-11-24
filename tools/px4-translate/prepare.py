#!/usr/bin/env python3
"""Prepare PX4 parameter metadata strings for machine translation.

Collects every English string fpl shows for PX4 parameters (shortDesc,
longDesc, enum value and bitmask labels) that has no approved translation in
the official PX4-Metadata-Translations .ts file, de-duplicates them by
(kind, text) and writes batches grouped by subsystem prefix, each with
context and the approved translations of the same subsystem as reference.

usage: prepare.py META_JSON OFFICIAL_TS OUTDIR [--batch-chars N]
  META_JSON    PX4ParameterFactMetaData.json (as cached by fpl)
  OFFICIAL_TS  parameters_<ll_CC>.ts from PX4-Metadata-Translations
  OUTDIR       writes items.json, batch_NNN.jsonl, batch_NNN.ref.jsonl
"""
import argparse
import collections
import json
import os
import xml.etree.ElementTree as ET


def squash(s):
    return " ".join((s or "").split())


def load_ts(path):
    out = {}
    for ctx in ET.parse(path).getroot().findall("context"):
        msg = ctx.find("message")
        tr = msg.find("translation")
        ok = tr.get("type") is None and (tr.text or "").strip() != ""
        out[ctx.find("name").text] = (msg.find("source").text or "", tr.text if ok else None)
    return out


def records(meta):
    """(key, kind, param, english) for every displayed string."""
    for p in meta:
        n = p["name"]
        if p.get("shortDesc"):
            yield f"/parameters/{n}/shortDesc", "short", n, p["shortDesc"]
        if p.get("longDesc"):
            yield f"/parameters/{n}/longDesc", "long", n, p["longDesc"]
        for v in p.get("values", []):
            if v.get("description"):
                yield f"/parameters/{n}/values/{v['value']}/description", "label", n, v["description"]
        for b in p.get("bitmask", []):
            if b.get("description"):
                yield f"/parameters/{n}/bitmask/{b['index']}/description", "label", n, b["description"]


def prefix(name):
    return name.split("_")[0]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("meta")
    ap.add_argument("ts")
    ap.add_argument("outdir")
    ap.add_argument("--batch-chars", type=int, default=9000)
    a = ap.parse_args()

    meta = json.load(open(a.meta))["parameters"]
    by_name = {p["name"]: p for p in meta}
    ts = load_ts(a.ts)

    items = {}  # (kind, english) -> item
    approved = collections.defaultdict(list)  # prefix -> [(en, uk)]
    for key, kind, name, en in records(meta):
        src, tr = ts.get(key, (None, None))
        if tr is not None and squash(src) == squash(en):
            if kind != "long":
                approved[prefix(name)].append((squash(en), squash(tr)))
            continue
        it = items.get((kind, en))
        if not it:
            p = by_name[name]
            ctx = f"parameter {name}"
            if kind == "label":
                ctx += f" ({p.get('shortDesc', '')}): label of an enum value / bitmask bit"
            elif kind == "short":
                ctx += f", group '{p.get('group', '')}': parameter title"
            else:
                ctx += f" ({p.get('shortDesc', '')}): description"
            it = items[(kind, en)] = {"id": len(items), "kind": kind, "text": en, "context": ctx,
                                      "keys": [], "prefix": prefix(name)}
        it["keys"].append(key)

    os.makedirs(a.outdir, exist_ok=True)
    allitems = sorted(items.values(), key=lambda i: (i["prefix"], i["id"]))
    json.dump(allitems, open(os.path.join(a.outdir, "items.json"), "w"), ensure_ascii=False, indent=0)

    batches, cur, size = [], [], 0
    for it in allitems:
        n = len(it["text"]) + len(it["context"])
        if cur and (size + n > a.batch_chars):
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
        refs, seen = [], set()
        for pre in sorted({it["prefix"] for it in b}):
            for en, uk in approved.get(pre, []):
                if en not in seen and len(refs) < 120:
                    seen.add(en)
                    refs.append({"en": en, "uk": uk})
        with open(base + ".ref.jsonl", "w") as f:
            for r in refs:
                f.write(json.dumps(r, ensure_ascii=False) + "\n")
    print(f"{len(allitems)} unique strings ({sum(len(i['keys']) for i in allitems)} records) "
          f"in {len(batches)} batches of <= {a.batch_chars} chars")


if __name__ == "__main__":
    main()
