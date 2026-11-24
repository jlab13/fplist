#!/usr/bin/env python3
"""Check that ArduPilot flight-mode names stayed in English.

Collects the enum labels of the flight-mode parameters (FLTMODEn, MODEn,
*_MODE lists that are mode tables) from the pdef files and reports
translations in OUTDIR/batch_*.out.jsonl that changed such a label.

usage: modes_check.py OUTDIR PDEF.xml [...] [--fix]
--fix rewrites the offending translations back to the English mode name.
"""
import glob
import json
import os
import re
import sys
import xml.etree.ElementTree as ET

MODE_PARAM = re.compile(r"^(FLTMODE\d|MODE\d|INITIAL_MODE|FS_.*_ACTION_MODE|Q_.*MODE_MAP)$")


def mode_names(paths):
    names = set()
    for path in paths:
        for p in ET.parse(path).getroot().iter("param"):
            name = (p.get("name") or "").split(":")[-1]
            if MODE_PARAM.match(name):
                names |= {" ".join(v.text.split()) for v in p.iter("value") if v.text}
    return names


def main():
    args = [a for a in sys.argv[1:] if a != "--fix"]
    fix = "--fix" in sys.argv
    outdir, pdefs = args[0], args[1:]
    modes = mode_names(pdefs)
    items = {i["id"]: i for i in json.load(open(os.path.join(outdir, "items.json")))}
    bad = 0
    for of in sorted(glob.glob(os.path.join(outdir, "batch_*.out.jsonl"))):
        rows = [json.loads(l) for l in open(of) if l.strip()]
        changed = False
        for r in rows:
            it = items[r["id"]]
            if it["kind"] == "label" and it["text"] in modes and r["text"] != it["text"]:
                bad += 1
                print(f"{os.path.basename(of)} id {r['id']}: {it['text']!r} -> {r['text']!r}")
                if fix:
                    r["text"] = it["text"]
                    changed = True
        if changed:
            with open(of, "w") as f:
                f.writelines(json.dumps(r, ensure_ascii=False) + "\n" for r in rows)
    print(f"{len(modes)} mode names, {bad} translated" + (" (fixed)" if fix and bad else ""))


if __name__ == "__main__":
    main()
