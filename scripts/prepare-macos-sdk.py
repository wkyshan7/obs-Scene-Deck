"""Prepare matching public OBS headers and official Qt SDK for an installed OBS.app."""
import argparse
import hashlib
import json
import plistlib
import subprocess
import tarfile
import urllib.request
from pathlib import Path


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
    qt = vendor["dependencies"]["qt6"]
    archive = dependencies / f"macos-deps-qt6-{qt['version']}-universal.tar.xz"
    expected = qt["hashes"]["macos-universal"]
    if not archive.exists():
        temporary = archive.with_suffix(".download")
        url = f"{qt['baseUrl']}/{qt['version']}/{archive.name}"
        with urllib.request.urlopen(url, timeout=120) as response, temporary.open("wb") as output:
            while block := response.read(1024 * 1024):
                output.write(block)
        if hashlib.sha256(temporary.read_bytes()).hexdigest() != expected:
            raise RuntimeError("Official Qt SDK SHA256 mismatch")
        temporary.replace(archive)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != expected:
        raise RuntimeError("Cached Qt SDK SHA256 mismatch")
    qt_root = dependencies / f"qt6-macos-{qt['version']}"
    marker = qt_root / ".scene-deck-sdk.sha256"
    if not marker.exists() or marker.read_text().strip() != expected:
        qt_root.mkdir(exist_ok=True)
        with tarfile.open(archive) as package:
            package.extractall(qt_root, filter="data")
        marker.write_text(expected)
    if not (qt_root / "lib/cmake/Qt6/Qt6Config.cmake").exists():
        raise RuntimeError("Qt archive layout does not contain lib/cmake/Qt6; inspect the SDK before building")
    cache = dependencies / "macos-sdk.cmake"
    cache.write_text(
        f'set(CMAKE_PREFIX_PATH "{qt_root.as_posix()}" CACHE STRING "" FORCE)\n'
        f'set(SCENE_DECK_OBS_SOURCE "{source.as_posix()}" CACHE PATH "" FORCE)\n'
        f'set(SCENE_DECK_OBS_APP "{app.as_posix()}" CACHE PATH "" FORCE)\n'
    )
    print(f"Prepared OBS {version} public headers and verified official Qt SDK. Cache: {cache}")


if __name__ == "__main__":
    main()
