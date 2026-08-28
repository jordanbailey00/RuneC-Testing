#!/usr/bin/env python3
"""Export B237 hitmark definitions and their concrete sprite assets."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

from PIL import Image

from rc_cache import (
    CONFIG_HITMARK,
    INDEX_CONFIGS,
    RcCacheStore,
    decode_hitmark_definition,
    load_sprite_group,
)

HITSPLAT_MAGIC = 0x54505348  # HSPT
HITSPLAT_VERSION = 1
RECORD = struct.Struct("<HhIhhhhbbH")
BASIC_HITMARK_IDS = (26, 27, 28, 29, 48, 68)


def concrete_hitmarks(store: RcCacheStore):
    files = store.read_group(INDEX_CONFIGS, CONFIG_HITMARK)
    definitions = []
    for hitmark_id, data in sorted(files.items()):
        definition = decode_hitmark_definition(hitmark_id, data)
        if not definition.complete:
            raise ValueError(
                f"hitmark {hitmark_id}: unknown opcode "
                f"{definition.unknown_opcode}"
            )
        if definition.middle_graphic_id < 0:
            continue
        if (definition.font_id >= 0
                or definition.class_graphic_id >= 0
                or definition.left_graphic_id >= 0
                or definition.right_graphic_id >= 0):
            raise ValueError(
                f"hitmark {hitmark_id}: unsupported multi-part/font definition"
            )
        if definition.text_format not in ("", "%1"):
            raise ValueError(
                f"hitmark {hitmark_id}: unsupported text format "
                f"{definition.text_format!r}"
            )
        definitions.append(definition)

    concrete_ids = {definition.hitmark_id for definition in definitions}
    missing = sorted(set(BASIC_HITMARK_IDS) - concrete_ids)
    if missing:
        raise ValueError(f"missing required B237 hitmarks: {missing}")
    return definitions


def export(args: argparse.Namespace) -> int:
    store = RcCacheStore(args.cache)
    definitions = concrete_hitmarks(store)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.sprites.mkdir(parents=True, exist_ok=True)

    for stale in args.sprites.glob("hitsplat_*.png"):
        stale.unlink()

    with args.output.open("wb") as out:
        out.write(struct.pack(
            "<III", HITSPLAT_MAGIC, HITSPLAT_VERSION, len(definitions)))
        for definition in definitions:
            flags = 1 if definition.text_format == "%1" else 0
            out.write(RECORD.pack(
                definition.hitmark_id,
                definition.middle_graphic_id,
                definition.text_color,
                definition.scroll_x,
                definition.scroll_y,
                definition.text_offset_y,
                definition.fade_start_cycle,
                definition.replacement_mode,
                flags,
                definition.display_cycles,
            ))

            sprites = load_sprite_group(store, definition.middle_graphic_id)
            if len(sprites) != 1:
                raise ValueError(
                    f"hitmark {definition.hitmark_id}: expected one sprite, "
                    f"found {len(sprites)}"
                )
            sprite = sprites[0]
            if sprite.width != 25 or sprite.height != 25:
                raise ValueError(
                    f"hitmark {definition.hitmark_id}: expected 25x25 sprite, "
                    f"found {sprite.width}x{sprite.height}"
                )
            image = Image.frombytes(
                "RGBA", (sprite.width, sprite.height), sprite.pixels)
            image.save(args.sprites / f"hitsplat_{definition.hitmark_id}.png")

    print(
        f"hitsplats: exported {len(definitions)} concrete B237 definitions "
        f"to {args.output} and {args.sprites}"
    )
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cache", type=Path, required=True)
    parser.add_argument(
        "--output", type=Path, default=Path("data/defs/hitsplats.bin"))
    parser.add_argument(
        "--sprites", type=Path, default=Path("data/sprites/ui"))
    return export(parser.parse_args())


if __name__ == "__main__":
    raise SystemExit(main())
