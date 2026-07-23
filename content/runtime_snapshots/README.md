# Runtime Snapshots

This directory contains tracked RuneC-owned runtime snapshots for first-release
datasets whose previous exporters depended on removed research mirrors or on
formal source-gap rows.

These files are not cache-derived assets. They are reviewed bridge inputs used
to preserve the current runtime behavior while the release pipeline becomes
self-contained. `tools/export_runtime_snapshots.py` verifies
`manifest.json`, installs selected files back into `data/`, and writes
source-clean reports for the migrated datasets.

Spatial NPC snapshots use the mapsquare-indexed NSPI runtime format. The
single full-world snapshot replaces both the row-scanned global NSPN file and
the duplicate Varrock-only slice; indexing changes storage and access cost,
not source rows or spawn ordering.

Do not add new snapshots casually. Prefer b237/cache-derived exporters or
human-readable content tables when the dataset can be rebuilt that way.
