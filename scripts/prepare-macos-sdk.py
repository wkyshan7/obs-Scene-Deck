"""Prepare matching OBS headers, Qt and obs-deps SDKs for an installed OBS.app."""
import argparse
import hashlib
import json
import plistlib
import subprocess
import tarfile
import urllib.request
from pathlib import Path


def prepare_dependency(dependencies, dependency, archive_prefix, directory_prefix, required):
    version = dependency["version"]
    archive = dependencies / f"{archive_prefix}-{version}-universal.tar.xz"
    expected = dependency["hashes"]["macos-universal"]
    if not archive.exists():
        temporary = archive.with_suffix(".download")
        url = f"{dependency['baseUrl']}/{version}/{archive.name}"
        with urllib.request.urlopen(url, timeout=120) as response, temporary.open("wb") as output:
            while block := response.read(1024 * 1024):
                output.write(block)
        with temporary.open("rb") as file:
            if hashlib.file_digest(file, "sha256").hexdigest() != expected:
                raise RuntimeError(f"Official SDK SHA256 mismatch: {archive.name}")
        temporary.replace(archive)
    with archive.open("rb") as file:
        if hashlib.file_digest(file, "sha256").hexdigest() != expected:
            raise RuntimeError(f"Cached SDK SHA256 mismatch: {archive.name}")
    root = dependencies / f"{directory_prefix}-{version}"
    marker = root / ".scene-deck-sdk.sha256"
    if not marker.exists() or marker.read_text().strip() != expected or not (root / required).is_file():
        root.mkdir(exist_ok=True)
        with tarfile.open(archive) as package:
            package.extractall(root, filter="data")
        if not (root / required).is_file():
            raise RuntimeError(f"SDK archive is missing {required}: {archive.name}")
        marker.write_text(expected)
    return root


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--obs-app", type=Path, default=Path("/Applications/OBS.app"))
    args = parser.parse_args()
    project = Path(__file__).resolve().parents[1]
    dependencies = project / ".deps"
    dependencies.mkdir(exist_ok=True)
    app = args.obs_app.resolve()
    with (app / "Contents/Info.plist").open("rb") as file:
        version = plistlib.load(file)["CFBundleShortVersionString"]
    source = dependencies / f"obs-studio-macos-{version}"
    if not source.exists():
        subprocess.run(["git", "clone", "--depth", "1", "--branch", version,
                        "https://github.com/obsproject/obs-studio.git", str(source)], check=True)
    actual = subprocess.check_output(["git", "-C", str(source), "describe", "--tags", "--exact-match", "HEAD"], text=True).strip()
    if actual != version:
        raise RuntimeError("OBS.app and public headers must have the same version")
    presets = json.loads((source / "CMakePresets.json").read_text())
    vendor = next(p for p in presets["configurePresets"] if p["name"] == "dependencies")["vendor"]["obsproject.com/obs-studio"]
    qt_root = prepare_dependency(dependencies, vendor["dependencies"]["qt6"],
                                 "macos-deps-qt6", "qt6-macos", "lib/cmake/Qt6/Qt6Config.cmake")
    prebuilt_root = prepare_dependency(dependencies, vendor["dependencies"]["prebuilt"],
                                       "macos-deps", "obs-deps-macos", "include/simde/x86/sse2.h")
    cache = dependencies / "macos-sdk.cmake"
    cache.write_text(
        f'set(CMAKE_PREFIX_PATH "{qt_root.as_posix()};{prebuilt_root.as_posix()}" CACHE STRING "" FORCE)\n'
        f'set(SCENE_DECK_OBS_SOURCE "{source.as_posix()}" CACHE PATH "" FORCE)\n'
        f'set(SCENE_DECK_OBS_APP "{app.as_posix()}" CACHE PATH "" FORCE)\n'
    )
    print(f"Prepared OBS {version} headers, Qt and prebuilt SDK (SIMDe), with verified SHA256. Cache: {cache}")


if __name__ == "__main__":
    main()
