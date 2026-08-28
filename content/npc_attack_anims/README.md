# NPC Attack Animations

`animations.tsv` maps an NPC's B237 standing sequence to the compatible basic
attack sequence used by the viewer when an attack event does not provide a
more specific animation. Exact event visuals, such as Jad's style-specific
attacks, remain authoritative.

Rows are limited to unambiguous B237 symbol-family matches reviewed against
the local cache/model dump. The human standing sequence uses the modern OSRS
default unarmed-punch behavior. A shared stance that can represent different
weapons, styles, or encounter phases is intentionally not guessed here; it
must use the existing per-NPC/style combat visual table. These references are
evidence only; the tracked RuneC table is the runtime authority.
