# Machine translation of PX4 parameter metadata

gndtui shows PX4 parameter descriptions in the user's language. Reviewed
community translations come from
[PX4/PX4-Metadata-Translations](https://github.com/PX4/PX4-Metadata-Translations)
(Crowdin). This directory fills the remaining gaps with machine translation,
stored in `translations/px4/parameters_<ll_CC>.mt.ts` and marked as such in the UI.
Community translations always win over machine ones.

## Pipeline

```sh
W=build/px4-mt; mkdir -p $W
# 1. inputs: metadata + community translation as cached by gndtui, and the PX4
#    translation rules (fetched, not stored here: the PX4 repo has no licence)
gh api repos/PX4/PX4-Metadata-Translations/contents/tools/crowdin/TRANSLATION_GUIDE.md --jq .content | base64 -d > $W/guide.md
gh api repos/PX4/PX4-Metadata-Translations/contents/tools/crowdin/terms/uk.md --jq .content | base64 -d > $W/uk_terms.md
cp tools/px4-translate/decisions.uk.md $W/decisions.md

# 2. untranslated strings, de-duplicated, batched by subsystem with context
#    and the approved translations of the same subsystem as reference
python3 tools/px4-translate/prepare.py ~/.cache/gndtui/px4-parameters.json \
    ~/.cache/gndtui/px4-parameters_uk_UA.ts $W --batch-chars 18000

# 3. translate every batch_NNN.jsonl into batch_NNN.out.jsonl ({"id","text"})
#    with an LLM following guide.md + uk_terms.md + decisions.md
#    (we used Claude Sonnet, one agent per batch)

# 4. mechanical checks (line breaks, identifiers, code, numbers, ...) and the
#    overlay file; then terminology drift between batches
python3 tools/px4-translate/collect.py $W translations/px4/parameters_uk_UA.mt.ts \
    --lang uk --model claude-sonnet
python3 tools/px4-translate/terms_check.py $W
```

`collect.py` leaves out any translation that fails a check and exits non-zero,
so a bad batch never reaches the overlay. `terms_check.py` only reports.

## Quality

Compared on hard descriptions, an LLM given the PX4 guide and term sheet was
clearly better than DeepL without a glossary (terminology, meaning of control
terms, decimal separator). It is still unreviewed machine output: review in
Crowdin before proposing it upstream.
