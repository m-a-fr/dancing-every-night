"""Comparaison de versions et lecture de la fiche latest.json (src/update.c), avec le compilateur C du système."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(shutil.which('cc'), 'no C compiler')
class UpdateTest(unittest.TestCase):
    def test_parse_and_compare(self):
        sdl = subprocess.run(['sdl2-config', '--cflags', '--libs'], capture_output=True, text=True)
        if sdl.returncode:
            self.skipTest('SDL2 not found')
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / 'update_harness'
            subprocess.run(['cc', '-std=c11', '-Wall', '-Werror', '-DGAME_VERSION="0.9.0"', '-DGAME_NAME="Test"', '-DGAME_SAVE_SLUG="t"',
                            str(ROOT / 'tests/update_harness.c'), str(ROOT / 'src/update.c'), '-o', str(exe), *sdl.stdout.split()],
                           check=True)
            out = subprocess.run([str(exe)], capture_output=True, text=True, check=True)
            self.assertIn('update ok', out.stdout)

    def test_manifest_is_valid(self):
        import json
        manifest = json.loads((ROOT / 'release/latest.json').read_text(encoding='utf-8'))
        for key in ('version', 'url', 'notes_en', 'notes_fr', 'vpk_url', 'sha256', 'size'):
            self.assertTrue(manifest[key], key)
        self.assertRegex(manifest['version'], r'^\d+\.\d+\.\d+$')
        self.assertRegex(manifest['sha256'], r'^[0-9a-f]{64}$')


if __name__ == '__main__':
    unittest.main()
