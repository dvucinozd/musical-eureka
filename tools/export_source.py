"""Export the pinned tracked source for local reference; never edit the source repo."""
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]
lock = json.loads((ROOT / "config/source-baseline.json").read_text(encoding="utf-8"))
source = Path(lock["local_source"])
destination = ROOT / "reference/nektar-p4"
if destination.exists():
    raise SystemExit("Reference directory already exists; refusing to overwrite it.")
resolved = subprocess.check_output(
    ["git", "-C", str(source), "rev-parse", lock["commit"] + "^{commit}"], text=True
).strip()
if resolved != lock["commit"]:
    raise SystemExit("Source revision mismatch")
archive = subprocess.check_output(
    ["git", "-C", str(source), "archive", "--format=zip", resolved, "--", *lock["archive_paths"]]
)
records = []
with zipfile.ZipFile(io.BytesIO(archive)) as bundle:
    # Validate the entire archive before creating any files.
    for info in bundle.infolist():
        path = PurePosixPath(info.filename)
        if path.is_absolute() or ".." in path.parts or "\\" in info.filename or ":" in info.filename:
            raise SystemExit("Unsafe archive member: " + info.filename)
        if ((info.external_attr >> 16) & 0o170000) == 0o120000:
            raise SystemExit("Symlinks are not supported: " + info.filename)
    for info in bundle.infolist():
        if info.is_dir():
            continue
        path = destination.joinpath(*PurePosixPath(info.filename).parts)
        path.parent.mkdir(parents=True, exist_ok=True)
        data = bundle.read(info)
        path.write_bytes(data)
        records.append({"path": info.filename, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()})
manifest = {"source_commit": resolved, "archive_sha256": hashlib.sha256(archive).hexdigest(), "files": records}
(ROOT / "reference/source-files.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
print(f"Exported {len(records)} pinned files to {destination}; no source working-tree files copied.")

