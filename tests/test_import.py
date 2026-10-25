"""Import réel ZIP/dossiers dans une sauvegarde temporaire, sans toucher aux chansons du joueur."""
import subprocess
import tempfile
import unittest
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

class ImportTest(unittest.TestCase):
    def test_zip_and_extracted_folders(self):
        flags = subprocess.check_output(['sdl2-config', '--cflags', '--libs'], text=True).split()
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            exe = root / 'import_test'
            subprocess.run(['cc', '-std=c11', '-DGAME_NAME="Test"', '-DGAME_VERSION="0"',
                            '-DGAME_SAVE_SLUG="test"', '-DMINIZ_NO_ARCHIVE_WRITING_APIS',
                            '-DMINIZ_NO_TIME', '-DMINIZ_NO_ZLIB_COMPATIBLE_NAMES',
                            str(ROOT/'tests/import_harness.c'), str(ROOT/'src/mapimport.c'),
                            str(ROOT/'src/files.c'), str(ROOT/'src/third_party/miniz.c'),
                            '-o', str(exe), *flags], check=True)
            incoming = root/'save/import'
            incoming.mkdir(parents=True)
            def song(path, song_id):
                path.mkdir(parents=True)
                (path/'easy.chart').write_text(f'ID {song_id}\n')
                (path/'audio.ogg').write_bytes(b'audio'*20000)
            song(incoming/'direct', 'direct')
            song(incoming/'wrapper'/'inner', 'wrapped')
            song(incoming/'bad'/'inner'/'extra', 'bad')
            song(incoming/'official', 'official')
            (root/'assets/songs/official').mkdir(parents=True)
            with zipfile.ZipFile(incoming/'archive.zip', 'w') as z:
                z.writestr('zipped/easy.chart', 'ID zipped\n')
                z.writestr('zipped/audio.ogg', b'zip audio')
            def run():
                return subprocess.check_output([str(exe)], cwd=root, text=True).strip()
            self.assertEqual(run(), '5 3 2')
            for name in ('direct', 'wrapped', 'zipped'):
                self.assertTrue((root/f'save/songs/{name}/easy.chart').is_file())
            self.assertEqual((root/'save/songs/direct/audio.ogg').read_bytes(), b'audio'*20000)
            self.assertTrue((incoming/'failed/bad').is_dir())
            self.assertTrue((incoming/'failed/official.txt').is_file())
            self.assertEqual(run(), '0 0 0')
            song(incoming/'replacement', 'direct')
            (incoming/'replacement/audio.ogg').write_bytes(b'new')
            self.assertEqual(run(), '1 1 0')
            self.assertEqual((root/'save/songs/direct/audio.ogg').read_bytes(), b'new')
            self.assertFalse((incoming/'replacement').exists())

if __name__ == '__main__':
    unittest.main()
