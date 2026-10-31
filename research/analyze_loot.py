"""Read-only reconstruction of loot rules for the pinned 2013 Alpha image.

Writes research evidence only. Never opens a game process or changes game files.
Run with --disassemble to reproduce listings, and --native for the x86 probe.
"""
from __future__ import annotations

import argparse
from collections import Counter
from datetime import datetime, timezone
from fractions import Fraction
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
EVIDENCE = ROOT / "research/evidence/loot-20261027"
GAME = ROOT / "playtest"
EXPECTED = "84a7a132a84d4282338e7ea45a1940d32066d64e39cbafed8f2418d5a6dc30bf"
DUMPER = Path(r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.41.34120\bin\Hostx64\x86\dumpbin.exe")
RANGES = {
    "death": (0x59B180, 0x59C480),
    "spawn-drop": (0x59AEB0, 0x59B180),
    "armor": (0x5F51E0, 0x5F56C0),
    "weapon": (0x5F8AD0, 0x5F8E10),
    "formula": (0x5F6D50, 0x5F6E20),
    "pet-food": (0x5F79E0, 0x5F7A60),
    "quality": (0x5F8530, 0x5F8590),
    "level-jitter": (0x4C7E80, 0x4C7F60),
    "mission": (0x5956C0, 0x596CA0),
    "display-name": (0x598A50, 0x59A020),
    "item-model-id": (0x51BE60, 0x51C1D0),
    "required-class": (0x4C6F20, 0x4C70B0),
    "color": (0x4C7D20, 0x4C7E80),
    "level-curve": (0x43CA60, 0x43CAA0),
    "inverse-curve": (0x4462F0, 0x446330),
    "stat-scaling": (0x445F60, 0x446070),
    "native-equipment": (0x5C7730, 0x5C8390),
    "initial-spawns": (0x5E4850, 0x5F0CE0),
    "mission-reseed": (0x60FCBC, 0x60FE80),
    "identify": (0x47C8D0, 0x47CB20),
}


class PE:
    def __init__(self, path: Path):
        self.data = path.read_bytes()
        header = struct.unpack_from("<I", self.data, 0x3C)[0]
        assert self.data[header:header + 4] == b"PE\0\0"
        machine, count, _, _, _, optional_size, _ = struct.unpack_from("<HHIIIHH", self.data, header + 4)
        assert machine == 0x14C
        self.base = struct.unpack_from("<I", self.data, header + 24 + 28)[0]
        self.sections = []
        for i in range(count):
            p = header + 24 + optional_size + i * 40
            name, virtual_size, va, size, offset = struct.unpack_from("<8sIIII", self.data, p)
            self.sections.append((va, size, offset))

    def read(self, va: int, size: int) -> bytes:
        rva = va - self.base
        for start, length, offset in self.sections:
            if start <= rva and rva + size <= start + length:
                p = offset + rva - start
                return self.data[p:p + size]
        raise ValueError(f"Unmapped VA {va:x}")

    def u16text(self, va: int) -> str:
        out = bytearray()
        for i in range(200):
            b = self.read(va + 2 * i, 2)
            if b == b"\0\0":
                return out.decode("utf-16-le")
            out.extend(b)
        raise ValueError("Unterminated string")


def f32(n: float) -> float:
    return struct.unpack("<f", struct.pack("<f", n))[0]


def curve(level: int | float) -> float:
    return f32(1.0 - f32(1.0 / f32(f32(f32(float(level) - 1.0) * f32(0.05)) + 1.0)))


def inverse(value: float) -> float:
    return f32(f32(f32(f32(1.0 / f32(1.0 - value)) - 1.0) * 20.0) + 1.0)


def power(level: int) -> int:
    return int(f32(f32(curve(level) * 100.0) + 1.0))


def jitter_bounds(level: int, allow_upward: bool) -> tuple[int, int]:
    fraction = f32(0.05)
    if level <= 1 or f32(curve(level) - fraction) <= 0:
        return 1, 1
    low = max(1, int(inverse(f32(curve(level) - fraction))))
    upper_curve = min(f32(0.9999), f32(curve(level) + fraction))
    high = int(inverse(upper_curve)) if allow_upward else level
    return low, high


def instructions(name: str):
    text = (EVIDENCE / f"{name}.asm.txt").read_text(encoding="utf-8-sig")
    pattern = re.compile(r"^\s*([0-9A-F]{8}):.*? {2,}([a-z][a-z0-9]*)\s*(.*)$")
    for line in text.splitlines():
        m = pattern.match(line)
        if m:
            yield int(m[1], 16), m[2], m[3]


def write_json(name, data):
    (EVIDENCE / name).write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--disassemble", action="store_true")
    parser.add_argument("--native", action="store_true")
    args = parser.parse_args()
    image = PE(GAME / "Cube.exe")
    assert hashlib.sha256(image.data).hexdigest() == EXPECTED, "Unsupported game image"
    EVIDENCE.mkdir(exist_ok=True)
    hashes_before = {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                     for p in [GAME / "Cube.exe", GAME / "MSVCR110.dll", GAME / "cube_cn.dll"]}
    assert hashes_before["MSVCR110.dll"] == "b30160e759115e24425b9bcdf606ef6ebce4657487525ede7f1ac40b90ff7e49"
    if args.disassemble:
        for name, (start, end) in RANGES.items():
            result = subprocess.run([str(DUMPER), "/nologo", "/disasm", f"/range:0x{start:x},0x{end:x}",
                                     str(GAME / "Cube.exe")], check=True, stdout=subprocess.PIPE)
            listing = result.stdout.decode("ascii").replace("\r\n", "\n")
            (EVIDENCE / f"{name}.asm.txt").write_text(listing, encoding="utf-8")
        crt_listing = subprocess.run([str(DUMPER), "/nologo", "/disasm", "/range:0x100a492a,0x100a495d",
                                      str(GAME / "MSVCR110.dll")], check=True, stdout=subprocess.PIPE)
        (EVIDENCE / "crt-rng.asm.txt").write_text(crt_listing.stdout.decode("ascii").replace("\r\n", "\n"),
                                                  encoding="utf-8")
        spawn_lines = (EVIDENCE / "initial-spawns.asm.txt").read_text(encoding="utf-8").splitlines()
        extracts = []
        for index, line in enumerate(spawn_lines):
            if re.search(r"call\s+005F8530", line):
                extracts.extend(spawn_lines[max(0, index - 12):index + 8] + [""])
        assert len(extracts) >= 7 * 20
        (EVIDENCE / "initial-equipment-quality-extract.asm.txt").write_text("\n".join(extracts), encoding="utf-8")
    if args.native:
        result = subprocess.run([str(ROOT / "build/loot_native_probe.exe"), str(GAME / "Cube.exe"),
                                 str(EVIDENCE)], check=True, stdout=subprocess.PIPE)
        checks = json.loads(result.stdout)
        assert checks["nativeGeneratorCases"] == 10000 and checks["nativeQualityCases"] == 120000
        # Confirm rejection happens before any copied code can execute.
        fixture = None
        try:
            with tempfile.NamedTemporaryFile(dir=ROOT / "build", prefix="loot-rejection-", suffix=".bin", delete=False) as f:
                fixture = Path(f.name).resolve()
                assert fixture.is_relative_to((ROOT / "build").resolve())
                altered = bytearray(image.data)
                altered[0x40] ^= 1
                f.write(altered)
            rejection = subprocess.run([str(ROOT / "build/loot_native_probe.exe"), str(fixture), str(EVIDENCE)],
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            assert rejection.returncode == 2 and b"Unsupported image hash" in rejection.stderr
            checks["modifiedImageRejected"] = True
        finally:
            if fixture is not None:
                assert fixture.is_relative_to((ROOT / "build").resolve())
                fixture.unlink()
        write_json("native-checks.json", checks)
    checks = json.loads((EVIDENCE / "native-checks.json").read_text(encoding="utf-8-sig"))
    # Raw bytes and instruction counts pin conclusions to the actual image.
    signatures = {
        "own_drop_boss_override": (0x59B34D, "66 85 73 7e 0f 84"),
        "extra_equipment_mod10": (0x59B442, "b9 0a 00 00 00 f7 f9"),
        "pet_food_type20": (0x5F7A2A, "c6 03 14"),
        "prefix_quality_byte": (0x599013, "0f b6 4b 0c 83 f9 04"),
        "prefix_seed_mod10": (0x599029, "b9 0a 00 00 00 f7 f1"),
        "recipient_class": (0x596A39, "0f b6 80 40 01 00 00"),
    }
    confirmed = {}
    for name, (va, hex_bytes) in signatures.items():
        if not hex_bytes:
            continue
        wanted = bytes.fromhex(hex_bytes)
        assert image.read(va, len(wanted)) == wanted, name
        confirmed[name] = {"va": hex(va), "bytes": wanted.hex(" ")}
    calls = {}
    for name, expected in [("armor", 21), ("weapon", 11)]:
        sites = [hex(va) for va, op, operands in instructions(name)
                 if op == "call" and operands == "004C7830"]
        assert len(sites) == expected, (name, len(sites))
        calls[name] = sites
    prefixes = []
    for va, op, operands in instructions("display-name"):
        if op != "push" or not re.fullmatch(r"[0-9A-F]+h", operands):
            continue
        target = int(operands[:-1], 16)
        if not 0x700000 <= target < 0x750000:
            continue
        try:
            text = image.u16text(target)
        except (ValueError, UnicodeError):
            continue
        if text.startswith("item:"):
            prefixes.append({"callsite": hex(va), "string_va": hex(target), "key": text})
    assert len(prefixes) == 50, len(prefixes)
    # Follow both original switch jump tables instead of assuming physical case order.
    prefix_by_site = {int(x["callsite"], 16): x for x in prefixes}
    name_instructions = list(instructions("display-name"))
    quality_targets = struct.unpack("<5I", image.read(0x599FFC, 20))
    prefix_table = []
    for q, case in enumerate(quality_targets):
        tables = [re.search(r"edx\*4\+([0-9A-F]+)h", operands)
                  for va, op, operands in name_instructions if case <= va < case + 50 and op == "jmp"]
        table = next(int(m[1], 16) for m in tables if m)
        targets = struct.unpack("<10I", image.read(table, 40))
        keys = []
        for seed_mod, target in enumerate(targets):
            assert target in prefix_by_site, (q, seed_mod, hex(target))
            keys.append({"seed_mod_10": seed_mod, **prefix_by_site[target]})
        prefix_table.append({"quality": q, "quality_case_va": hex(case), "seed_jump_table_va": hex(table), "keys": keys})
    write_json("prefixes.json", prefix_table)

    class_names = {1: "战士", 2: "游侠", 3: "法师", 4: "盗贼"}
    weapon_slots = {1: 3, 2: 2, 3: 3, 4: 3}
    armor_slots = {1: 4, 2: 5, 3: 5, 4: 5}
    class_rates = []
    for cl in class_names:
        specific = Fraction(weapon_slots[cl], 22) + Fraction(armor_slots[cl], 42)
        with_jewelry = specific + Fraction(1, 21)
        class_rates.append({"class": class_names[cl], "weapon_candidates": weapon_slots[cl],
                            "armor_candidates": armor_slots[cl],
                            "class_specific_nominal_fraction": str(specific),
                            "class_specific_nominal_percent": float(specific * 100),
                            "including_unrestricted_jewelry_nominal_percent": float(with_jewelry * 100)})
    domain = checks["uniformEffectiveStateDomain"]
    quality = [{"rank": r, "uniform_state_percent": [100 * n / domain for n in row],
                "boss_initial_equipment_quality": min(r + 1, 4)}
               for r, row in enumerate(checks["qualityCounts"])]
    levels = [{"enemy_level": level, "enemy_power": power(level),
               "direct_drop_possible_level_range": [min(jitter_bounds(max(1, level - 1), True)[0],
                                                       jitter_bounds(level, True)[0]),
                                                    max(jitter_bounds(max(1, level - 1), True)[1],
                                                       jitter_bounds(level, True)[1])],
               "native_equipment_nonboss_jitter": jitter_bounds(level, False)}
              for level in [1, 5, 10, 20, 30, 50, 100, 200]]
    constants = {hex(va): struct.unpack("<f", image.read(va, 4))[0]
                 for va in [0x6FCDB4, 0x6FCD74, 0x745DC0, 0x702A3C]}
    assert constants["0x6fcd74"] == 20.0 and constants["0x745dc0"] == 1.0
    assert 0.04999 < constants["0x6fcdb4"] < 0.05001
    assert 0.9998 < constants["0x702a3c"] < 1.0
    hashes_after = {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                    for p in [GAME / "Cube.exe", GAME / "MSVCR110.dll", GAME / "cube_cn.dll"]}
    assert hashes_before == hashes_after
    result = {
        "captured_utc": datetime.now(timezone.utc).isoformat(),
        "image_hash": EXPECTED, "static_input_hashes_before": hashes_before,
        "static_input_hashes_after": hashes_after,
        "source_commit": subprocess.check_output(["git", "-C", str(ROOT / "external/CubeWorld-Reversal"),
                                                   "rev-parse", "HEAD"], text=True).strip(),
        "raw_signature_checks": confirmed, "candidate_append_sites": calls,
        "native_checks": checks, "nominal_class_distribution": class_rates,
        "quality_distribution": quality, "level_examples": levels, "curve_constants": constants,
        "limitations": [
            "Native checks execute audited copies in an isolated helper; vector, copy, memory and RNG calls are shimmed.",
            "Uniform-state quality counts are not measured boss-kill rates or independence assumptions.",
            "Class fractions are candidate-pool weights; CRT modulo bias and channel conditioning are separate.",
            "No game process was opened by these tools; no gameplay or multiplayer combination test was performed.",
        ],
    }
    write_json("analysis.json", result)
    print(json.dumps({"native": checks["nativeGeneratorCases"], "quality": checks["nativeQualityCases"],
                      "prefixes": len(prefixes), "class_percent": class_rates,
                      "levels": levels}, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
