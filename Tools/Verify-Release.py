"""Verify final package hashes and corresponding source against this checkout."""
import hashlib
import json
import pathlib
import sys
import zipfile

project, build, release = (pathlib.Path(p).resolve() for p in sys.argv[1:4])
version = sys.argv[4] if len(sys.argv) > 4 else "0.4.2-rc.1"

def digest(data):
    return hashlib.sha256(data).hexdigest()

manifest = json.loads((release / "SHA256.json").read_text(encoding="utf-8-sig"))
for entry in manifest:
    target = (release / entry["path"]).resolve()
    if not target.is_relative_to(release):
        raise RuntimeError("Invalid manifest path")
    if digest(target.read_bytes()) != entry["sha256"].lower():
        raise RuntimeError("Package hash mismatch: " + entry["path"])

source_name = f"TDSpeed-Source-{version}.zip"
source_path = release / source_name
source_bytes = source_path.read_bytes()
with zipfile.ZipFile(source_path) as archive:
    inventory = json.loads(archive.read("SOURCE-SHA256.json"))
    excluded = {"out", ".git", ".vs", "__pycache__", "x64"}
    current = {
        p.relative_to(project).as_posix(): digest(p.read_bytes())
        for p in project.rglob("*")
        if p.is_file() and not any(part in excluded for part in p.relative_to(project).parts)
    }
    if inventory != current:
        differences = sorted(k for k in inventory.keys() | current.keys() if inventory.get(k) != current.get(k))
        raise RuntimeError("Source differs from current project: " + ", ".join(differences[:10]))
    for name, expected in inventory.items():
        if digest(archive.read(name)) != expected:
            raise RuntimeError("Archived source mismatch: " + name)
    for required in ("LICENSE", ".github/workflows/windows.yml", "Tests/PluginHostSmoke.cpp", "docs/VALIDATION.md"):
        if required not in inventory:
            raise RuntimeError("Required source entry missing: " + required)

app = build / "TDSpeedApp_artefacts/Release/TDSpeedPreview.exe"
plugin = build / "TDSpeedVST_artefacts/Release/VST3/TDSpeedPreview.vst3/Contents/x86_64-win/TDSpeedPreview.vst3"

def check_package(name, filename, expected):
    with zipfile.ZipFile(release / name) as archive:
        entries = [n for n in archive.namelist() if pathlib.PurePosixPath(n.replace("\\", "/")).name == filename and not n.endswith("/")]
        if len(entries) != 1 or digest(archive.read(entries[0])) != digest(expected):
            raise RuntimeError("Archive content mismatch: " + name + " / " + filename)

check_package(f"TDSpeed-Portable-{version}.zip", "TDSpeedPreview.exe", app.read_bytes())
check_package(f"TDSpeed-VST3-{version}.zip", "TDSpeedPreview.vst3", plugin.read_bytes())
for package in (f"TDSpeed-Portable-{version}.zip", f"TDSpeed-VST3-{version}.zip"):
    check_package(package, source_name, source_bytes)

print(json.dumps({"passed": True, "manifestEntries": len(manifest), "sourceFiles": len(inventory),
                  "appSha256": digest(app.read_bytes()), "vst3Sha256": digest(plugin.read_bytes()),
                  "sourceSha256": digest(source_bytes)}, indent=2))
