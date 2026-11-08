"""Offline checks for SDK integrity and complete macOS dependency preparation."""
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import plistlib
import tarfile
import tempfile
import unittest
from unittest.mock import patch

SCRIPT = Path(__file__).resolve().parents[1] / "scripts/prepare-macos-sdk.py"
spec = importlib.util.spec_from_file_location("macos_sdk", SCRIPT)
sdk = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sdk)


class MacosSdkTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.project = Path(self.temporary.name)
        self.dependencies = self.project / ".deps"
        self.dependencies.mkdir()

    def archive(self, prefix, required):
        archive = self.dependencies / f"{prefix}-test-universal.tar.xz"
        with tarfile.open(archive, "w:xz") as package:
            data = b"test SDK file\n"
            info = tarfile.TarInfo(required)
            info.size = len(data)
            package.addfile(info, io.BytesIO(data))
        return {"version": "test", "baseUrl": "https://invalid.example",
                "hashes": {"macos-universal": hashlib.sha256(archive.read_bytes()).hexdigest()}}

    def test_main_prepares_qt_and_simde_and_writes_both_prefixes(self):
        qt = self.archive("macos-deps-qt6", "lib/cmake/Qt6/Qt6Config.cmake")
        prebuilt = self.archive("macos-deps", "include/simde/x86/sse2.h")
        source = self.dependencies / "obs-studio-macos-test"
        source.mkdir()
        vendor = {"obsproject.com/obs-studio": {"dependencies": {"qt6": qt, "prebuilt": prebuilt}}}
        (source / "CMakePresets.json").write_text(json.dumps({"configurePresets": [
            {"name": "dependencies", "vendor": vendor}]}))
        app = self.project / "OBS.app"
        (app / "Contents").mkdir(parents=True)
        (app / "Contents/Info.plist").write_bytes(plistlib.dumps({"CFBundleShortVersionString": "test"}))
        with patch.object(sdk, "__file__", str(self.project / "scripts/prepare-macos-sdk.py")), \
                patch("sys.argv", [str(SCRIPT), "--obs-app", str(app)]), \
                patch.object(sdk.subprocess, "check_output", return_value="test\n"):
            sdk.main()
        cache = (self.dependencies / "macos-sdk.cmake").read_text()
        self.assertIn("qt6-macos-test;", cache)
        self.assertIn("obs-deps-macos-test", cache)
        self.assertTrue((self.dependencies / "obs-deps-macos-test/include/simde/x86/sse2.h").is_file())

    def test_corrupted_archive_is_rejected(self):
        dependency = self.archive("macos-deps", "include/simde/x86/sse2.h")
        (self.dependencies / "macos-deps-test-universal.tar.xz").write_bytes(b"corrupted")
        with self.assertRaisesRegex(RuntimeError, "SHA256 mismatch"):
            sdk.prepare_dependency(self.dependencies, dependency, "macos-deps", "prebuilt", "include/simde/x86/sse2.h")

    def test_missing_header_repairs_extraction_even_with_matching_marker(self):
        dependency = self.archive("macos-deps", "include/simde/x86/sse2.h")
        root = sdk.prepare_dependency(self.dependencies, dependency, "macos-deps", "prebuilt", "include/simde/x86/sse2.h")
        (root / "include/simde/x86/sse2.h").unlink()
        sdk.prepare_dependency(self.dependencies, dependency, "macos-deps", "prebuilt", "include/simde/x86/sse2.h")
        self.assertTrue((root / "include/simde/x86/sse2.h").is_file())

    def test_incomplete_sdk_does_not_get_success_marker(self):
        dependency = self.archive("macos-deps", "include/unrelated.h")
        with self.assertRaisesRegex(RuntimeError, "missing include/simde"):
            sdk.prepare_dependency(self.dependencies, dependency, "macos-deps", "prebuilt", "include/simde/x86/sse2.h")
        self.assertFalse((self.dependencies / "prebuilt-test/.scene-deck-sdk.sha256").exists())


if __name__ == "__main__":
    unittest.main()
