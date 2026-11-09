"""Verify the preserved bring-up artifacts without opening a device."""
import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parents[1]
manifest_path = root / "docs/evidence/2026-11-09-bringup-kit.json"
manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
kit = root / manifest["archive_directory"]
failures = []
for profile in manifest["profiles"]:
    for artifact in profile["artifacts"]:
        path = kit / profile["id"] / artifact["path"]
        if not path.is_file():
            failures.append(f"Missing: {path}")
            continue
        data = path.read_bytes()
        if len(data) != artifact["bytes"] or hashlib.sha256(data).hexdigest() != artifact["sha256"]:
            failures.append(f"Size or SHA-256 mismatch: {path}")
if failures:
    raise SystemExit("\n".join(failures))
print(f"PASS: {len(manifest['profiles'])} preserved ESP32-S31 profiles at {kit}")
