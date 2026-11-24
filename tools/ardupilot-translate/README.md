# Machine translation of ArduPilot parameter metadata

ArduPilot publishes its parameter documentation (`apm.pdef.xml`) only in
English and has no community translation. gndtui ships a machine translation
in `translations/ardupilot/parameters_<ll_CC>.mt.ts`, shown with a
"machine translation" mark in the details pane.

Unlike PX4 (keyed by parameter path), these translations are keyed by the
**English text** (`<context><name>$text/human|desc|label</name>`): one
translation covers every vehicle (Copter, Plane, Rover, Sub, …) and firmware
version that uses the same sentence. If ArduPilot changes a description, the
old translation simply stops matching and the English text is shown.

## Pipeline

```sh
W=build/apm-mt; mkdir -p $W
# metadata: master for every vehicle + the latest stable versions
for v in ArduCopter ArduPlane Rover ArduSub AntennaTracker Blimp Heli; do
  curl -sfL --compressed -o ~/.cache/gndtui/apm-$v-master.pdef.xml \
       https://autotest.ardupilot.org/Parameters/$v/apm.pdef.xml
done
# (optionally versioned/<Copter|Plane|Rover|Sub>/stable-X.Y.Z/apm.pdef.xml)

# translation rules: the PX4 guide and Ukrainian term sheet apply to
# ArduPilot too, plus our decisions (mode names stay in English, ...)
gh api repos/PX4/PX4-Metadata-Translations/contents/tools/crowdin/TRANSLATION_GUIDE.md --jq .content | base64 -d > $W/guide.md
gh api repos/PX4/PX4-Metadata-Translations/contents/tools/crowdin/terms/uk.md --jq .content | base64 -d > $W/uk_terms.md
cp tools/ardupilot-translate/decisions.uk.md $W/decisions.md

python3 tools/ardupilot-translate/prepare.py $W ~/.cache/gndtui/apm-*.pdef.xml

# translate batch_NNN.jsonl -> batch_NNN.out.jsonl ({"id","text"}) with an LLM
# (we used Claude Sonnet, one agent per batch, following the three files above)

python3 tools/px4-translate/collect.py $W translations/ardupilot/parameters_uk_UA.mt.ts \
    --lang uk --model claude-sonnet
python3 tools/px4-translate/terms_check.py $W
```

The validation (`collect.py`) and terminology report (`terms_check.py`) are
shared with the PX4 pipeline in `tools/px4-translate/`.
