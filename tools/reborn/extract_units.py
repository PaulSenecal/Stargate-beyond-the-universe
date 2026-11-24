"""Summarize the ships of the Stargate Reborn mod (Sins of a Solar Empire II)
and convert their stats to the scale of our ship library.

The mod files stay local (stargate/src/autre, git-ignored): this script only
reads them. Usage, from the repository root:

    python3 -I tools/reborn/extract_units.py stargate/src/autre/entities units.csv

Columns: raw stats (hull, armor, shield, dps from every weapon), hangars,
kamikaze flag, abilities, then `game_hull` / `game_dps`, our scale:
the Ha'tak is the reference (3000 hull + 1200 armor + 2000 shield = 6200,
96 dps in the mod; 420 hull, ~1.5 dps here). Strikecraft live in squadrons
in the mod, their conversion is meaningless: tune them by hand.
"""
import csv
import json
import os
import sys
from collections import Counter

REF_EHP, REF_DPS = 6200.0, 96.0        # Ha'tak in the mod
GAME_HULL, GAME_DPS = 420.0, 1.47      # Ha'tak in our library
SKIPPED = ('_orbital_', '_planet_', 'combat_dummy', '_item_', 'unity_agent')


def load(folder, name):
    try:
        with open(os.path.join(folder, name), encoding='utf-8') as f:
            return json.load(f)
    except (OSError, ValueError):
        return None


def abilities_of(unit):
    names = []
    for group in unit.get('abilities') or []:
        if isinstance(group, dict):
            names += group.get('abilities', [])
    return names


def main(src, out):
    rows = []
    for fn in sorted(os.listdir(src)):
        if not fn.endswith('.unit') or any(s in fn for s in SKIPPED):
            continue
        unit = load(src, fn)
        if not unit or 'health' not in unit:
            continue
        kind = unit.get('target_filter_unit_type', '')
        level = (unit['health'].get('levels') or [{}])[0]
        hull = level.get('max_hull_points', 0)
        armor = level.get('max_armor_points', 0)
        shield = level.get('max_shield_points', 0)

        dps, weapons = 0.0, []
        for wid, n in Counter(w.get('weapon') for w in (unit.get('weapons') or {}).get('weapons', [])).items():
            w = load(src, wid + '.weapon') or {}
            damage, cooldown = w.get('damage', 0), w.get('cooldown_duration', 0) or 0
            if cooldown:
                dps += n * damage / cooldown
            weapons.append(f"{n}x {wid} [{damage} dmg / {cooldown} s, range {w.get('range', 0)}, "
                           f"{'/'.join(w.get('tags', []))}]")

        carrier = unit.get('carrier') or {}
        small = kind in ('strikecraft', 'torpedo')
        rows.append({
            'unit': fn[:-5],
            'faction': fn.split('_')[0] if not fn.startswith('sgr_npc_') else fn.split('_')[2],
            'type': kind,
            'hull': hull, 'armor': armor, 'shield': shield,
            'shield_regen': level.get('shield_point_restore_rate', 0),
            'dps': round(dps, 1),
            'squadrons': carrier.get('base_max_squadron_capacity', 0),
            'strikecraft': ','.join(carrier.get('strikecraft_kinds', [])),
            'kamikaze': 'torpedo' in unit,
            'game_hull': '' if small else round((hull + armor + shield) * GAME_HULL / REF_EHP),
            'game_dps': '' if small else round(dps * GAME_DPS / REF_DPS, 2),
            'abilities': ' '.join(abilities_of(unit)),
            'weapons': ' | '.join(weapons),
        })

    with open(out, 'w', newline='', encoding='utf-8') as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    print(f'{len(rows)} ships -> {out}')


if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
