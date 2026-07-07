#!/usr/bin/env python3
"""Audit world NPC/object rows that cannot currently load or render.

This is a diagnostic report, not a data exporter. It compares the generated
runtime artifacts against the viewer/runtime load rules and buckets missing
content by cause, region, and id/name.
"""
from __future__ import annotations

import argparse
import contextlib
import io
import os
import struct
import sys
from collections import Counter, defaultdict
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PIPELINE = ROOT / "tools/cache_pipeline"

NDEF_MAGIC = 0x4E444546
NSPN_MAGIC = 0x4E53504E
NSPN_FLAG_INSTANCE = 0x01
ODEF_MAGIC = 0x4645444F
OPLC_MAGIC = 0x434C504F
MDL2_MAGIC = 0x4D444C32
MDL3_MAGIC = 0x4D444C33
OANM_MAGIC = 0x4D4E414F
LOCAL_CACHE_ROOT = "data"
LOCAL_CACHE_PATH = LOCAL_CACHE_ROOT + "/source/b237-openrs2-2528/cache"


@dataclass(frozen=True)
class NpcDef:
    name: str


@dataclass(frozen=True)
class ObjectDef:
    name: str
    actions: tuple[str, ...]
    model_count: int
    transforms: tuple[int, ...]


@dataclass(frozen=True)
class MissingExample:
    ident: int
    name: str
    x: int
    y: int
    plane: int
    extra: str = ""


@dataclass
class CauseBucket:
    total: int = 0
    regions: Counter[tuple[int, int]] = field(default_factory=Counter)
    ids: Counter[tuple[int, str]] = field(default_factory=Counter)
    rows: list[MissingExample] = field(default_factory=list)
    examples: dict[tuple[int, int], list[MissingExample]] = field(
        default_factory=lambda: defaultdict(list)
    )


def read_exact(f, size: int, path: Path) -> bytes:
    data = f.read(size)
    if len(data) != size:
        raise EOFError(f"short read in {path}")
    return data


def read_pstr(data: bytes, pos: int, size_fmt: str = "<H") -> tuple[str, int]:
    size = struct.calcsize(size_fmt)
    length = struct.unpack_from(size_fmt, data, pos)[0]
    pos += size
    text = data[pos:pos + length].decode("latin-1", errors="replace")
    return text, pos + length


def parse_npc_defs(path: Path) -> dict[int, NpcDef]:
    data = path.read_bytes()
    magic, version, count = struct.unpack_from("<III", data, 0)
    if magic != NDEF_MAGIC or version not in (1, 2, 3, 4):
        raise ValueError(f"{path}: unsupported NDEF header")
    pos = 12
    out: dict[int, NpcDef] = {}
    for _ in range(count):
        npc_id = struct.unpack_from("<I", data, pos)[0]
        pos += 4
        pos += 1 + 2 + 2 + 12 + 20
        name_len = data[pos]
        pos += 1
        name = data[pos:pos + name_len].decode("latin-1", errors="replace")
        pos += name_len
        if version >= 2:
            pos += 10
        if version >= 3:
            model_count = data[pos]
            pos += 1 + model_count * 4
        if version >= 4:
            for _slot in range(5):
                option_len = data[pos]
                pos += 1 + option_len
        out[npc_id] = NpcDef(name=name)
    return out


def iter_npc_spawns(path: Path):
    with path.open("rb") as f:
        magic, version, count = struct.unpack("<III", read_exact(f, 12, path))
        if magic != NSPN_MAGIC or version not in (1, 2):
            raise ValueError(f"{path}: unsupported NSPN header")
        for _ in range(count):
            npc_id = struct.unpack("<I", read_exact(f, 4, path))[0]
            x = struct.unpack("<i", read_exact(f, 4, path))[0]
            y = struct.unpack("<i", read_exact(f, 4, path))[0]
            plane = struct.unpack("<B", read_exact(f, 1, path))[0]
            direction = struct.unpack("<B", read_exact(f, 1, path))[0]
            wander = struct.unpack("<B", read_exact(f, 1, path))[0]
            flags = struct.unpack("<B", read_exact(f, 1, path))[0] if version >= 2 else 0
            yield npc_id, x, y, plane, direction, wander, flags


def parse_model_ids(path: Path) -> set[int]:
    data = path.read_bytes()
    magic, count = struct.unpack_from("<II", data, 0)
    if magic not in (MDL2_MAGIC, MDL3_MAGIC):
        raise ValueError(f"{path}: unsupported model header")
    ids: set[int] = set()
    offsets = struct.unpack_from("<" + "I" * count, data, 8) if count else ()
    for offset in offsets:
        if offset and offset + 4 <= len(data):
            ids.add(struct.unpack_from("<I", data, offset)[0])
    return ids


def parse_object_defs(path: Path) -> dict[int, ObjectDef]:
    data = path.read_bytes()
    magic, version, count = struct.unpack_from("<III", data, 0)
    if magic != ODEF_MAGIC or version not in (1, 2):
        raise ValueError(f"{path}: unsupported ODEF header")
    pos = 12
    row_fmt = "<IHHBBBBBiiiII"
    row_size = struct.calcsize(row_fmt)
    extra_fmt = "<BBHiHH"
    extra_size = struct.calcsize(extra_fmt)
    out: dict[int, ObjectDef] = {}
    for _ in range(count):
        row = struct.unpack_from(row_fmt, data, pos)
        pos += row_size
        obj_id = row[0]
        model_count = row[5]
        transform_count = row[6]
        param_count = 0
        if version >= 2:
            _supports_items, _clip_flags, param_count, _ambient_sound_id, \
                _ambient_sound_distance, _ambient_sound_retain = \
                struct.unpack_from(extra_fmt, data, pos)
            pos += extra_size
        name, pos = read_pstr(data, pos)
        actions: list[str] = []
        for _slot in range(5):
            action, pos = read_pstr(data, pos)
            if action:
                actions.append(action)
        pos += model_count * 4
        transforms = tuple(
            struct.unpack_from("<" + "i" * transform_count, data, pos)
        ) if transform_count else ()
        pos += transform_count * 4
        pos += param_count * 8
        out[obj_id] = ObjectDef(
            name=name,
            actions=tuple(actions),
            model_count=model_count,
            transforms=transforms,
        )
    return out


def iter_object_placements(path: Path):
    with path.open("rb") as f:
        magic, version, count, _region_count = struct.unpack(
            "<IIII", read_exact(f, 16, path)
        )
        if magic != OPLC_MAGIC or version not in (1, 2):
            raise ValueError(f"{path}: unsupported OPLC header")
        for i in range(count):
            obj_id = struct.unpack("<I", read_exact(f, 4, path))[0]
            if version >= 2:
                key = struct.unpack("<Q", read_exact(f, 8, path))[0]
            else:
                key = i
            x = struct.unpack("<H", read_exact(f, 2, path))[0]
            y = struct.unpack("<H", read_exact(f, 2, path))[0]
            mapsquare = struct.unpack("<H", read_exact(f, 2, path))[0]
            plane = struct.unpack("<B", read_exact(f, 1, path))[0]
            obj_type = struct.unpack("<B", read_exact(f, 1, path))[0]
            rotation = struct.unpack("<B", read_exact(f, 1, path))[0]
            flags = struct.unpack("<B", read_exact(f, 1, path))[0]
            yield obj_id, key, x, y, mapsquare, plane, obj_type, rotation, flags


def add_missing(
    buckets: dict[tuple[str, str], CauseBucket],
    content: str,
    cause: str,
    ident: int,
    name: str,
    x: int,
    y: int,
    plane: int,
    extra: str = "",
) -> None:
    bucket = buckets[(content, cause)]
    region = x >> 6, y >> 6
    bucket.total += 1
    bucket.regions[region] += 1
    bucket.ids[(ident, name)] += 1
    bucket.rows.append(MissingExample(ident, name, x, y, plane, extra))
    examples = bucket.examples[region]
    if len(examples) < 3:
        examples.append(MissingExample(ident, name, x, y, plane, extra))


def find_cache_dir(explicit: Path | None) -> Path | None:
    if explicit:
        return explicit if explicit.exists() else None
    for key in ("RUNEC_B237_CACHE", "RUNEC_CACHE"):
        value = os.environ.get(key)
        if value and Path(value).exists():
            return Path(value)
    local = ROOT / LOCAL_CACHE_PATH
    return local if local.exists() else None


def load_visual_object_defs(cache_dir: Path | None):
    if not cache_dir:
        return None, None, None, None
    sys.path.insert(0, str(PIPELINE))
    from export_objects import (  # type: ignore
        EXPORTED_TYPES,
        decode_loc_definitions_modern,
        merge_runtime_object_visual_fallbacks,
        model_key_for_type,
        resolve_visual_loc,
    )
    from rc_cache import RcCacheStore  # type: ignore

    reader = RcCacheStore(cache_dir)
    with contextlib.redirect_stdout(io.StringIO()):
        loc_defs = decode_loc_definitions_modern(reader)
        merge_runtime_object_visual_fallbacks(loc_defs)
    return loc_defs, model_key_for_type, resolve_visual_loc, set(EXPORTED_TYPES)


def audit_npcs(
    buckets: dict[tuple[str, str], CauseBucket],
    npc_defs_path: Path,
    spawns_path: Path,
    npc_models_path: Path,
    strict_static_instance_skip: bool,
    cache_dir: Path | None,
) -> tuple[int, int]:
    npc_defs = parse_npc_defs(npc_defs_path)
    npc_model_ids = parse_model_ids(npc_models_path)
    cache_model_ids: dict[int, list[int]] = {}
    if cache_dir:
        sys.path.insert(0, str(PIPELINE))
        from rc_cache import (  # type: ignore
            CONFIG_NPC,
            RcCacheStore,
            decode_npc_definition,
            read_config_group,
        )
        files = read_config_group(RcCacheStore(cache_dir), CONFIG_NPC)
        for cache_npc_id, data in files.items():
            cache_def = decode_npc_definition(cache_npc_id, data)
            if cache_def.complete:
                cache_model_ids[cache_npc_id] = list(cache_def.models)
    rows = 0
    missing = 0
    for npc_id, x, y, plane, _direction, _wander, flags in iter_npc_spawns(spawns_path):
        rows += 1
        npc_def = npc_defs.get(npc_id)
        name = npc_def.name if npc_def else f"npc_{npc_id}"
        if strict_static_instance_skip and (flags & NSPN_FLAG_INSTANCE):
            add_missing(
                buckets, "npc", "instance_filtered_static_spawn",
                npc_id, name, x, y, plane, "NSPN_FLAG_INSTANCE",
            )
            missing += 1
        elif npc_def is None:
            add_missing(
                buckets, "npc", "missing_npc_definition",
                npc_id, name, x, y, plane,
            )
            missing += 1
        elif npc_id not in npc_model_ids and cache_model_ids.get(npc_id):
            add_missing(
                buckets, "npc", "missing_npc_render_model",
                npc_id, name, x, y, plane,
            )
            missing += 1
    return rows, missing


def object_missing_cause(
    obj_id: int,
    obj_type: int,
    object_defs: dict[int, ObjectDef],
    loc_defs,
    model_key_for_type,
    resolve_visual_loc,
    exported_types: set[int] | None,
) -> str | None:
    obj_def = object_defs.get(obj_id)
    if obj_def is None:
        return "missing_object_definition"
    if exported_types is not None and obj_type not in exported_types:
        return "unsupported_placement_shape"
    if loc_defs is None or model_key_for_type is None:
        if obj_def.model_count <= 0:
            return (
                "transform_object_no_default_model"
                if obj_def.transforms else "no_visual_model_in_object_def"
            )
        return None

    loc = loc_defs.get(obj_id)
    if loc is None:
        return (
            "transform_object_no_default_model"
            if obj_def.transforms else "no_visual_loc_definition"
        )
    visual_loc = resolve_visual_loc(loc, loc_defs, obj_type)
    if visual_loc is None:
        return (
            "transform_object_no_default_model"
            if obj_def.transforms else "no_model_for_placement_type"
        )
    model_ids = model_key_for_type(visual_loc, obj_type)
    if not model_ids:
        return (
            "transform_object_no_default_model"
            if obj_def.transforms else "no_model_for_placement_type"
        )
    return None


def audit_objects(
    buckets: dict[tuple[str, str], CauseBucket],
    object_defs_path: Path,
    object_placements_path: Path,
    cache_dir: Path | None,
) -> tuple[int, int, int]:
    object_defs = parse_object_defs(object_defs_path)
    loc_defs, model_key_for_type, resolve_visual_loc, exported_types = \
        load_visual_object_defs(cache_dir)
    rows = 0
    missing = 0
    for obj_id, _key, x, y, _mapsquare, plane, obj_type, rotation, _flags in \
            iter_object_placements(object_placements_path):
        rows += 1
        cause = object_missing_cause(
            obj_id, obj_type, object_defs, loc_defs, model_key_for_type,
            resolve_visual_loc,
            exported_types,
        )
        if cause:
            obj_def = object_defs.get(obj_id)
            name = obj_def.name if obj_def else f"object_{obj_id}"
            extra = f"type={obj_type} rot={rotation}"
            add_missing(
                buckets, "object", cause, obj_id, name, x, y, plane, extra,
            )
            missing += 1
    return rows, missing, 1 if loc_defs is not None else 0


def count_oanim_rows(path: Path) -> int:
    if not path.exists():
        return 0
    with path.open("rb") as f:
        magic, version, count = struct.unpack("<III", read_exact(f, 12, path))
        if magic != OANM_MAGIC or version != 1:
            return 0
        return count


def write_text_report(
    path: Path,
    buckets: dict[tuple[str, str], CauseBucket],
    npc_rows: int,
    npc_missing: int,
    object_rows: int,
    object_missing: int,
    used_cache: int,
) -> None:
    lines: list[str] = []
    lines.append("Missing world content audit")
    lines.append("===========================")
    lines.append("")
    lines.append(f"NPC spawn rows scanned: {npc_rows}")
    lines.append(f"NPC rows blocked/missing: {npc_missing}")
    lines.append(f"Object placement rows scanned: {object_rows}")
    lines.append(f"Object rows with no current visual path: {object_missing}")
    lines.append(
        "Object visual classification: "
        + (
            "b237 cache typed model rules plus runtime object_defs visual fallbacks"
            if used_cache else "object_defs model-count fallback"
        )
    )
    varrock_oanim = count_oanim_rows(ROOT / "data/regions/varrock.oanim")
    lines.append(f"Fixed Varrock animated object sidecar rows: {varrock_oanim}")
    lines.append("")

    for (content, cause), bucket in sorted(
        buckets.items(), key=lambda item: (-item[1].total, item[0])
    ):
        lines.append(f"{content}.{cause}: {bucket.total}")
        lines.append("  top regions:")
        for (rx, ry), count in bucket.regions.most_common(12):
            sample = "; ".join(
                f"{e.name or '<blank>'}#{e.ident}@{e.x},{e.y},p{e.plane}"
                + (f" ({e.extra})" if e.extra else "")
                for e in bucket.examples[(rx, ry)]
            )
            lines.append(f"    {rx},{ry}: {count}  {sample}")
        lines.append("  top ids:")
        for (ident, name), count in bucket.ids.most_common(12):
            lines.append(f"    {ident}\t{name or '<blank>'}\t{count}")
        lines.append("")

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines).rstrip() + "\n")


def write_region_report(
    path: Path,
    buckets: dict[tuple[str, str], CauseBucket],
) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w") as f:
        f.write("content\tcause\tregion_x\tregion_y\tcount\texamples\n")
        for (content, cause), bucket in sorted(buckets.items()):
            for (rx, ry), count in sorted(bucket.regions.items()):
                examples = "; ".join(
                    f"{e.name or '<blank>'}#{e.ident}@{e.x},{e.y},p{e.plane}"
                    + (f" ({e.extra})" if e.extra else "")
                    for e in bucket.examples[(rx, ry)]
                )
                f.write(f"{content}\t{cause}\t{rx}\t{ry}\t{count}\t{examples}\n")


def write_id_report(
    path: Path,
    buckets: dict[tuple[str, str], CauseBucket],
) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w") as f:
        f.write("content\tcause\tid\tname\tcount\n")
        for (content, cause), bucket in sorted(buckets.items()):
            for (ident, name), count in bucket.ids.most_common():
                f.write(f"{content}\t{cause}\t{ident}\t{name or '<blank>'}\t{count}\n")


def write_row_report(
    path: Path,
    buckets: dict[tuple[str, str], CauseBucket],
) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w") as f:
        f.write("content\tcause\tid\tname\tx\ty\tplane\tregion_x\tregion_y\textra\n")
        for (content, cause), bucket in sorted(buckets.items()):
            for row in bucket.rows:
                f.write(
                    f"{content}\t{cause}\t{row.ident}\t{row.name or '<blank>'}"
                    f"\t{row.x}\t{row.y}\t{row.plane}\t{row.x >> 6}"
                    f"\t{row.y >> 6}\t{row.extra}\n"
                )


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--npc-defs", type=Path, default=ROOT / "data/defs/npc_defs.bin")
    ap.add_argument("--npc-spawns", type=Path, default=ROOT / "data/spawns/world.npc-spawns.bin")
    ap.add_argument("--npc-models", type=Path, default=ROOT / "data/models/npcs.models")
    ap.add_argument("--object-defs", type=Path, default=ROOT / "data/defs/object_defs.bin")
    ap.add_argument("--object-placements", type=Path, default=ROOT / "data/defs/object_placements.bin")
    ap.add_argument("--cache", type=Path, default=None)
    ap.add_argument(
        "--strict-static-instance-skip",
        action="store_true",
        help="count NSPN_FLAG_INSTANCE rows as missing under conservative static loading",
    )
    ap.add_argument("--report", type=Path, default=ROOT / "tools/reports/missing_world_content.txt")
    ap.add_argument("--regions-report", type=Path, default=ROOT / "tools/reports/missing_world_content_regions.tsv")
    ap.add_argument("--ids-report", type=Path, default=ROOT / "tools/reports/missing_world_content_ids.tsv")
    ap.add_argument("--rows-report", type=Path, default=ROOT / "tools/reports/missing_world_content_rows.tsv")
    args = ap.parse_args()

    cache_dir = find_cache_dir(args.cache)
    buckets: dict[tuple[str, str], CauseBucket] = defaultdict(CauseBucket)
    npc_rows, npc_missing = audit_npcs(
        buckets, args.npc_defs, args.npc_spawns, args.npc_models,
        args.strict_static_instance_skip, cache_dir,
    )
    object_rows, object_missing, used_cache = audit_objects(
        buckets, args.object_defs, args.object_placements, cache_dir,
    )
    write_text_report(
        args.report, buckets, npc_rows, npc_missing, object_rows,
        object_missing, used_cache,
    )
    write_region_report(args.regions_report, buckets)
    write_id_report(args.ids_report, buckets)
    write_row_report(args.rows_report, buckets)
    print(f"wrote {args.report}")
    print(f"wrote {args.regions_report}")
    print(f"wrote {args.ids_report}")
    print(f"wrote {args.rows_report}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
