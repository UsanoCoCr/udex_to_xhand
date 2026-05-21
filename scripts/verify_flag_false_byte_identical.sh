#!/usr/bin/env bash
# Usage: bash scripts/verify_flag_false_byte_identical.sh
#
# M8a Step A.0' flag-gating regression guard.
#
# Verifies: with mapping.use_new_retarget=false in config.yaml, the regen-ed
# baseline fixture's "left" / "right" 12-rad arrays are byte-identical to the
# FROZEN M7 reference (tests/fixtures/mapper_baseline_m7_frozen.json, committed
# at M8a Step A.0 end and never rewritten).
#
# Side-effect free: backs up config.yaml + tests/fixtures/mapper_baseline.json
# before the experiment, restores both unconditionally on exit.
#
# Field names note: the plan §3 M8a Step A.0' snippet referenced "left_rad" /
# "right_rad" — the actual fixture schema uses bare "left" / "right" (see
# scripts/dump_mapper_baseline.py line 77-78 + tests/fixtures/mapper_baseline.json).
# This script follows the real schema.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

REFERENCE="tests/fixtures/mapper_baseline_m7_frozen.json"
LIVE_FIXTURE="tests/fixtures/mapper_baseline.json"
LIVE_CONFIG="config.yaml"

if [ ! -f "$REFERENCE" ]; then
    echo "ERROR: $REFERENCE missing — was M8a Step A.0 freeze step performed?" >&2
    exit 2
fi

BACKUP_CONFIG="$(mktemp -t verify_flag.config.XXXXXX)"
BACKUP_FIXTURE="$(mktemp -t verify_flag.fixture.XXXXXX)"
REGEN_FIXTURE="$(mktemp -t verify_flag.regen.XXXXXX)"

restore() {
    cp "$BACKUP_CONFIG" "$LIVE_CONFIG"
    cp "$BACKUP_FIXTURE" "$LIVE_FIXTURE"
    rm -f "$BACKUP_CONFIG" "$BACKUP_FIXTURE" "$REGEN_FIXTURE"
}
trap restore EXIT

cp "$LIVE_CONFIG" "$BACKUP_CONFIG"
cp "$LIVE_FIXTURE" "$BACKUP_FIXTURE"

# Force mapping.use_new_retarget = false in the live config (whatever it is now).
python3 - <<'PY'
import yaml
with open("config.yaml") as f:
    cfg = yaml.safe_load(f)
if "mapping" not in cfg or "use_new_retarget" not in cfg["mapping"]:
    raise SystemExit("mapping.use_new_retarget missing — A.0 plumbing not in place")
cfg["mapping"]["use_new_retarget"] = False
with open("config.yaml", "w") as f:
    yaml.safe_dump(cfg, f, sort_keys=False, allow_unicode=True)
PY

# Regen baseline through the Python oracle (the same code path that authored
# the frozen reference). Write to a scratch file so we never touch the live
# fixture between flag flips.
python3 scripts/dump_mapper_baseline.py \
    --example example.json \
    --config  "$LIVE_CONFIG" \
    --out     "$REGEN_FIXTURE" >/dev/null

# Compare the floating-point payload (ignore generated_at / sha / python_version).
set +e
python3 - "$REFERENCE" "$REGEN_FIXTURE" <<'PY'
import json, sys
ref = json.load(open(sys.argv[1]))
reg = json.load(open(sys.argv[2]))
ok = (ref["left"] == reg["left"]) and (ref["right"] == reg["right"])
sys.exit(0 if ok else 1)
PY
RC=$?
set -e

if [ "$RC" -eq 0 ]; then
    echo "flag=false byte-identical to M5b/M7 baseline (OK)"
else
    echo "ERROR: flag=false output diverges from M7 frozen reference — flag-gating broken" >&2
fi
exit "$RC"
