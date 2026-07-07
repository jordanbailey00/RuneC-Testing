#!/usr/bin/env python3
"""Audit broad visual/rendering gaps not covered by object id resolution.

This complements ``audit_missing_world_content.py``. The older audit answers:
"does every generated NPC/object row have a loadable definition/model path?"

This audit answers broader viewer questions:
- which terrain tiles are skipped or simplified by the current terrain exporter;
- whether generated scene-cache windows have assets for planes that contain NPCs;
- whether static ground-item world spawns have any committed export/load path.
"""
from __future__ import annotations

import argparse
import contextlib
import io
import os
import re
import struct
import sys
from collections import Counter, defaultdict
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"
PIPELINE = ROOT / "tools/cache_pipeline"
LOCAL_CACHE_ROOT = "data"
LOCAL_CACHE_PATH = LOCAL_CACHE_ROOT + "/source/b237-openrs2-2528/cache"
SCENE_RE = re.compile(r"^scene_(\d+)_(\d+)_r(\d+)(?:\.p([0-3]))?\.(terrain|objects|atlas)$")
GSPN_MAGIC = 0x4E505347


@dataclass(frozen=True)
class Example:
    ident: int
    name: str
    x: int
    y: int
    plane: int
    extra: str = ""


@dataclass
class Bucket:
    total: int = 0
    regions: Counter[tuple[int, int]] = field(default_factory=Counter)
    ids: Counter[tuple[int, str]] = field(default_factory=Counter)
    rows: list[Example] = field(default_factory=list)
    examples: dict[tuple[int, int], list[Example]] = field(
        default_factory=lambda: defaultdict(list)
    )


@dataclass
class ScenePrefix:
    origin_x: int
    origin_y: int
    radius_regions: int
    prefix: Path
    planes: dict[int, set[str]] = field(default_factory=lambda: defaultdict(set))

    @property
    def width(self) -> int:
        return (self.radius_regions * 2 + 1) * 64

    @property
    def height(self) -> int:
        return (self.radius_regions * 2 + 1) * 64

    def contains(self, x: int, y: int) -> bool:
        return (
            self.origin_x <= x < self.origin_x + self.width
            and self.origin_y <= y < self.origin_y + self.height
        )


def add_bucket(
    buckets: dict[tuple[str, str], Bucket],
    content: str,
    cause: str,
    ident: int,
    name: str,
    x: int,
    y: int,
    plane: int,
    extra: str = "",
    keep_rows: int = 5000,
) -> None:
    bucket = buckets[(content, cause)]
    region = (x >> 6, y >> 6)
    bucket.total += 1
    bucket.regions[region] += 1
    bucket.ids[(ident, name)] += 1
    if len(bucket.rows) < keep_rows:
        bucket.rows.append(Example(ident, name, x, y, plane, extra))
    examples = bucket.examples[region]
    if len(examples) < 3:
        examples.append(Example(ident, name, x, y, plane, extra))


def find_cache_dir(explicit: Path | None) -> Path | None:
    if explicit:
        return explicit if explicit.exists() else None
    for key in ("RUNEC_B237_CACHE", "RUNEC_CACHE"):
        value = os.environ.get(key)
        if value and Path(value).exists():
            return Path(value)
    local = ROOT / LOCAL_CACHE_PATH
    return local if local.exists() else None


def discover_scene_prefixes(scene_dir: Path) -> dict[tuple[int, int, int], ScenePrefix]:
    scenes: dict[tuple[int, int, int], ScenePrefix] = {}
    if not scene_dir.exists():
        return scenes
    for path in scene_dir.iterdir():
        match = SCENE_RE.match(path.name)
        if not match:
            continue
        ox, oy, radius, plane_s, ext = match.groups()
        key = (int(ox), int(oy), int(radius))
        scene = scenes.get(key)
        if scene is None:
            scene = ScenePrefix(
                origin_x=key[0],
                origin_y=key[1],
                radius_regions=key[2],
                prefix=scene_dir / f"scene_{key[0]}_{key[1]}_r{key[2]}",
            )
            scenes[key] = scene
        scene.planes[int(plane_s) if plane_s is not None else 0].add(ext)
    return scenes


def scene_regions(scenes: dict[tuple[int, int, int], ScenePrefix]) -> set[tuple[int, int]]:
    out: set[tuple[int, int]] = set()
    for scene in scenes.values():
        min_rx = scene.origin_x >> 6
        min_ry = scene.origin_y >> 6
        count = scene.radius_regions * 2 + 1
        for rx in range(min_rx, min_rx + count):
            for ry in range(min_ry, min_ry + count):
                out.add((rx, ry))
    return out


def scene_planes_by_region(
    scenes: dict[tuple[int, int, int], ScenePrefix],
) -> dict[tuple[int, int], set[int]]:
    out: dict[tuple[int, int], set[int]] = defaultdict(set)
    for scene in scenes.values():
        min_rx = scene.origin_x >> 6
        min_ry = scene.origin_y >> 6
        count = scene.radius_regions * 2 + 1
        scene_planes = set(scene.planes) or {0}
        for rx in range(min_rx, min_rx + count):
            for ry in range(min_ry, min_ry + count):
                out[(rx, ry)].update(scene_planes)
    return out


def load_terrain_helpers(cache_dir: Path):
    sys.path.insert(0, str(PIPELINE))
    from export_terrain import (  # type: ignore
        decode_floor_definitions_modern,
        load_texture_average_colors_modern,
        parse_terrain_full,
        visual_source_plane,
    )
    from rc_cache import (  # type: ignore
        RcCacheStore,
        find_all_map_region_files,
        read_map_region_file,
    )

    store = RcCacheStore(cache_dir)
    with contextlib.redirect_stdout(io.StringIO()):
        underlays, overlays = decode_floor_definitions_modern(store)
        tex_colors = load_texture_average_colors_modern(store)
    return (
        store,
        find_all_map_region_files,
        read_map_region_file,
        parse_terrain_full,
        visual_source_plane,
        underlays,
        overlays,
        tex_colors,
    )


def tile_has_nearby_floor(rt, z: int, tx: int, ty: int,
                          underlays, overlays) -> bool:
    for radius in (1, 2, 3):
        for nx in range(max(0, tx - radius), min(64, tx + radius + 1)):
            for ny in range(max(0, ty - radius), min(64, ty + radius + 1)):
                if abs(nx - tx) != radius and abs(ny - ty) != radius:
                    continue
                uid = rt.underlay_ids[z][nx][ny]
                oid = rt.overlay_ids[z][nx][ny] & 0xFFFF
                if uid > 0 and (uid - 1) in underlays:
                    return True
                if oid > 0:
                    overlay = overlays.get(oid - 1)
                    if overlay is not None and overlay.rgb != 0xFF00FF:
                        return True
    return False


def audit_terrain(
    buckets: dict[tuple[str, str], Bucket],
    cache_dir: Path | None,
    scenes: dict[tuple[int, int, int], ScenePrefix],
    scope: str,
    max_regions: int,
) -> tuple[int, int, int]:
    if cache_dir is None:
        add_bucket(
            buckets,
            "terrain",
            "b237_cache_unavailable",
            0,
            "b237 cache",
            0,
            0,
            0,
            "cannot audit terrain cache coverage",
        )
        return 0, 0, 0

    (
        store,
        find_all_map_region_files,
        read_map_region_file,
        parse_terrain_full,
        visual_source_plane,
        underlays,
        overlays,
        tex_colors,
    ) = load_terrain_helpers(cache_dir)

    all_regions = find_all_map_region_files(store)
    if scope == "all-cache":
        wanted = sorted(
            (entry.region_x, entry.region_y)
            for entry in all_regions.values()
            if entry.has_terrain
        )
    else:
        wanted = sorted(scene_regions(scenes))

    if max_regions > 0:
        wanted = wanted[:max_regions]

    region_planes = scene_planes_by_region(scenes) if scope == "scene-cache" else {}
    terrain_regions = 0
    missing_region_files = 0
    tiles = 0
    for rx, ry in wanted:
        data = read_map_region_file(store, rx, ry, "terrain")
        if data is None:
            missing_region_files += 1
            add_bucket(
                buckets,
                "terrain",
                "cache_region_terrain_file_missing",
                (rx << 8) | ry,
                "mapsquare",
                rx * 64,
                ry * 64,
                0,
            )
            continue
        terrain_regions += 1
        rt = parse_terrain_full(data, rx * 64, ry * 64)
        planes = sorted(region_planes.get((rx, ry), {0}))
        for plane in planes:
            for tx in range(64):
                for ty in range(64):
                    tiles += 1
                    source_plane = visual_source_plane(rt, tx, ty, plane)
                    uid = rt.underlay_ids[source_plane][tx][ty]
                    oid = rt.overlay_ids[source_plane][tx][ty] & 0xFFFF
                    x = rx * 64 + tx
                    y = ry * 64 + ty
                    shape = rt.shapes[source_plane][tx][ty]
                    rotation = rt.rotations[source_plane][tx][ty]

                    if uid == 0 and oid == 0:
                        if not tile_has_nearby_floor(
                                rt, source_plane, tx, ty, underlays, overlays):
                            add_bucket(
                                buckets,
                                "terrain",
                                "empty_terrain_no_floor_context",
                                0,
                                "empty terrain tile",
                                x,
                                y,
                                plane,
                                f"source_plane={source_plane}",
                            )
                        continue

                    if uid > 0 and (uid - 1) not in underlays:
                        add_bucket(
                            buckets,
                            "terrain",
                            "underlay_definition_missing",
                            uid - 1,
                            "underlay",
                            x,
                            y,
                            plane,
                            f"source_plane={source_plane}",
                        )

                    if oid <= 0:
                        continue

                    overlay = overlays.get(oid - 1)
                    if overlay is None:
                        add_bucket(
                            buckets,
                            "terrain",
                            "overlay_definition_missing",
                            oid - 1,
                            "overlay",
                            x,
                            y,
                            plane,
                            f"source_plane={source_plane} shape={shape} rot={rotation}",
                        )
                        continue

                    if overlay.rgb == 0xFF00FF:
                        if uid <= 0 and not tile_has_nearby_floor(
                                rt, source_plane, tx, ty, underlays, overlays):
                            add_bucket(
                                buckets,
                                "terrain",
                                "void_overlay_no_floor_context",
                                oid - 1,
                                "void overlay",
                                x,
                                y,
                                plane,
                                f"source_plane={source_plane} shape={shape} rot={rotation}",
                            )
                            continue

                    if overlay.texture >= 0 and overlay.texture not in tex_colors:
                        add_bucket(
                            buckets,
                            "terrain",
                            "texture_average_missing",
                            overlay.texture,
                            "texture",
                            x,
                            y,
                            plane,
                            f"overlay={oid - 1} source_plane={source_plane}",
                        )

    return terrain_regions, missing_region_files, tiles


def audit_scene_npcs(
    buckets: dict[tuple[str, str], Bucket],
    scenes: dict[tuple[int, int, int], ScenePrefix],
    cache_dir: Path | None,
) -> tuple[int, int, int, int]:
    sys.path.insert(0, str(TOOLS))
    import audit_missing_world_content as world_audit  # type: ignore

    npc_defs = world_audit.parse_npc_defs(ROOT / "data/defs/npc_defs.bin")
    npc_models = world_audit.parse_model_ids(ROOT / "data/models/npcs.models")
    spawns = list(world_audit.iter_npc_spawns(ROOT / "data/spawns/world.npc-spawns.bin"))
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

    scene_npc_rows = 0
    missing_plane_assets = 0
    missing_models = 0
    ankou_rows = 0

    for scene in scenes.values():
        for npc_id, x, y, plane, _direction, _wander, _flags in spawns:
            if not scene.contains(x, y):
                continue
            npc_def = npc_defs.get(npc_id)
            name = npc_def.name if npc_def else f"npc_{npc_id}"
            scene_npc_rows += 1
            if "ankou" in name.lower():
                ankou_rows += 1
            assets = scene.planes.get(plane, set())
            if not {"terrain", "objects", "atlas"}.issubset(assets):
                missing_plane_assets += 1
                add_bucket(
                    buckets,
                    "scene_npc",
                    "plane_assets_missing_for_spawn",
                    npc_id,
                    name,
                    x,
                    y,
                    plane,
                    f"scene={scene.prefix.name} assets={','.join(sorted(assets)) or 'none'}",
                )
            if npc_id not in npc_models and cache_model_ids.get(npc_id):
                missing_models += 1
                add_bucket(
                    buckets,
                    "scene_npc",
                    "full_npc_model_missing_for_spawn",
                    npc_id,
                    name,
                    x,
                    y,
                    plane,
                    f"scene={scene.prefix.name}",
                )
    return len(scenes), scene_npc_rows, missing_plane_assets, ankou_rows


def static_ground_item_count(path: Path) -> int | None:
    try:
        with path.open("rb") as f:
            header = f.read(12)
    except OSError:
        return None
    if len(header) != 12:
        return None
    magic, _version, count = struct.unpack("<III", header)
    if magic != GSPN_MAGIC:
        return None
    return int(count)


def audit_static_ground_items(
    buckets: dict[tuple[str, str], Bucket],
) -> tuple[int, int]:
    candidates = [
        ROOT / "data/spawns/world.ground-items.bin",
    ]
    present = [path for path in candidates if path.exists()]
    if present:
        rows = 0
        for path in present:
            count = static_ground_item_count(path)
            if count is None:
                add_bucket(
                    buckets,
                    "ground_item",
                    "static_world_spawn_export_invalid",
                    0,
                    path.relative_to(ROOT).as_posix(),
                    0,
                    0,
                    0,
                    "binary header is missing or invalid",
                )
                continue
            rows += count
        if rows == 0:
            add_bucket(
                buckets,
                "ground_item",
                "static_world_spawn_source_empty",
                0,
                "all static ground item spawns",
                0,
                0,
                0,
                "export/load path exists, but approved source has zero rows",
            )
        return len(present), rows

    add_bucket(
        buckets,
        "ground_item",
        "no_static_world_spawn_pipeline",
        0,
        "all static ground item spawns",
        0,
        0,
        0,
        "no committed item-spawn binary/exporter/active-area loader found",
    )
    return 0, 1


def write_text_report(
    path: Path,
    buckets: dict[tuple[str, str], Bucket],
    terrain_scope: str,
    terrain_regions: int,
    missing_terrain_regions: int,
    terrain_tiles: int,
    scene_count: int,
    scene_npc_rows: int,
    scene_missing_plane_assets: int,
    scene_ankou_rows: int,
    static_item_exports: int,
    static_item_rows: int,
) -> None:
    lines: list[str] = []
    lines.append("Rendering gap audit")
    lines.append("===================")
    lines.append("")
    lines.append(f"Terrain scope: {terrain_scope}")
    lines.append(f"Terrain cache regions scanned: {terrain_regions}")
    lines.append(f"Terrain region files missing: {missing_terrain_regions}")
    lines.append(f"Terrain tiles scanned: {terrain_tiles}")
    lines.append(f"Generated scene prefixes scanned: {scene_count}")
    lines.append(f"NPC rows inside generated scene prefixes: {scene_npc_rows}")
    lines.append(
        "NPC rows on planes missing generated scene assets: "
        f"{scene_missing_plane_assets}"
    )
    lines.append(f"Ankou rows inside generated scene prefixes: {scene_ankou_rows}")
    lines.append(f"Static ground-item spawn export files present: {static_item_exports}")
    lines.append(f"Static ground-item spawn rows exported: {static_item_rows}")
    lines.append("")
    lines.append("Important interpretation:")
    lines.append("- Terrain buckets are visual-risk buckets, not all gameplay blockers.")
    lines.append(
        "- `empty_terrain_no_floor_context` and `void_overlay_no_floor_context` "
        "are cache spaces with no nearby floor fallback; these are tracked so "
        "intentional empty/void space is separated from exporter holes."
    )
    lines.append(
        "- `static_world_spawn_source_empty` means the binary/export/load path "
        "exists, but loose static item rows such as coins/bars are still "
        "source-blocked."
    )
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


def write_rows(path: Path, buckets: dict[tuple[str, str], Bucket]) -> None:
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
    ap.add_argument("--cache", type=Path, default=None)
    ap.add_argument(
        "--terrain-scope",
        choices=("scene-cache", "all-cache"),
        default="all-cache",
        help="scan generated scene windows or every b237 mapsquare",
    )
    ap.add_argument(
        "--max-terrain-regions",
        type=int,
        default=0,
        help="debug limit; 0 scans all selected regions",
    )
    ap.add_argument(
        "--scene-dir",
        type=Path,
        default=ROOT / "data/regions/scene_cache",
    )
    ap.add_argument(
        "--report",
        type=Path,
        default=ROOT / "tools/reports/rendering_gaps.txt",
    )
    ap.add_argument(
        "--rows-report",
        type=Path,
        default=ROOT / "tools/reports/rendering_gaps_rows.tsv",
    )
    args = ap.parse_args()

    cache_dir = find_cache_dir(args.cache)
    scenes = discover_scene_prefixes(args.scene_dir)
    buckets: dict[tuple[str, str], Bucket] = defaultdict(Bucket)

    terrain_regions, missing_terrain_regions, terrain_tiles = audit_terrain(
        buckets, cache_dir, scenes, args.terrain_scope, args.max_terrain_regions,
    )
    scene_count, scene_npc_rows, scene_missing_plane_assets, scene_ankou_rows = \
        audit_scene_npcs(buckets, scenes, cache_dir)
    static_item_exports, static_item_rows = audit_static_ground_items(buckets)

    write_text_report(
        args.report,
        buckets,
        args.terrain_scope,
        terrain_regions,
        missing_terrain_regions,
        terrain_tiles,
        scene_count,
        scene_npc_rows,
        scene_missing_plane_assets,
        scene_ankou_rows,
        static_item_exports,
        static_item_rows,
    )
    write_rows(args.rows_report, buckets)
    print(f"wrote {args.report}")
    print(f"wrote {args.rows_report}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
