"""Read-only checks for this startup package; no hardware or network access."""
import hashlib
import json
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
errors = []
markdown = [root / "README.md", root / "AGENTS.md", root / "THIRD_PARTY.md", root / "reference/README.md", *root.glob("docs/**/*.md")]
for path in markdown:
    content = path.read_text(encoding="utf-8")
    for link in re.findall(r"\[[^\]]*\]\(([^)]+)\)", content):
        if re.match(r"(?:https?://|#)", link):
            continue
        target = link.split("#", 1)[0]
        if not (path.parent / target).exists():
            errors.append(f"Broken local link: {path.relative_to(root)} -> {target}")

baseline = json.loads((root / "config/source-baseline.json").read_text(encoding="utf-8"))
manifest = json.loads((root / "reference/source-files.json").read_text(encoding="utf-8"))
if manifest["source_commit"] != baseline["commit"]:
    errors.append("Source manifest revision mismatch")
reference = root / "reference/nektar-p4"
if reference.exists():
    for item in manifest["files"]:
        path = reference / item["path"]
        if not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != item["sha256"]:
            errors.append("Reference changed/missing: " + item["path"])

partitions = []
for line in (root / "config/partitions.proposed.csv").read_text(encoding="utf-8").splitlines():
    if not line.strip() or line.lstrip().startswith("#"):
        continue
    name, kind, subtype, offset, size, *_ = [field.strip() for field in line.split(",")]
    start, length = int(offset, 0), int(size, 0)
    if start % 4096 or length % 4096 or length <= 0:
        errors.append(f"Invalid partition alignment/size: {name}")
    if kind == "app" and start % 65536:
        errors.append(f"Invalid app alignment: {name}")
    if start + length > 16 * 1024 * 1024:
        errors.append(f"Partition exceeds 16 MiB: {name}")
    partitions.append((start, start + length, name))
partitions.sort()
for previous, current in zip(partitions, partitions[1:]):
    if previous[1] > current[0]:
        errors.append(f"Partition overlap: {previous[2]} / {current[2]}")
if errors:
    raise SystemExit("\n".join(errors))
print(f"PASS: {len(markdown)} Markdown files, local links, source manifest and {len(partitions)} proposed partitions.")
print(f"Reference hashes: {'verified' if reference.exists() else 'not checked (local export absent)'}; no hardware accessed.")

