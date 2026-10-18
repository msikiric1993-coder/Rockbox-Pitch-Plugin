import pathlib, zipfile, hashlib, json, sys

project = pathlib.Path(sys.argv[1]).resolve()
target = pathlib.Path(sys.argv[2]).resolve()
excluded = {"out", ".git", ".vs", "__pycache__", "x64"}
files = sorted(p for p in project.rglob("*") if p.is_file() and not any(part in excluded for part in p.relative_to(project).parts))
manifest = {p.relative_to(project).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest() for p in files}
with zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
    for p in files:
        archive.write(p, p.relative_to(project).as_posix())
    archive.writestr("SOURCE-SHA256.json", json.dumps(manifest, indent=2))
with zipfile.ZipFile(target) as archive:
    for name, digest in manifest.items():
        if hashlib.sha256(archive.read(name)).hexdigest() != digest:
            raise RuntimeError("Source verification failed: " + name)
    assert ".github/workflows/windows.yml" in archive.namelist()
print("Verified source archive:", len(files), "files")
