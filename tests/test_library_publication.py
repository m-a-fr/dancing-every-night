"""Isolated publication lifecycle; no live catalogue or R2 writes."""
import argparse
import json
from pathlib import Path
import sys
import tempfile
from unittest.mock import patch
from urllib.parse import urlparse
import urllib.request
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import catalog as cat
import library_publish as pub
import library_fetch as fetch
import library_ledger as ledger
import library_submitter as submitter
import library_site as site


class PublicationTest(unittest.TestCase):
    def test_private_attachment_authentication(self):
        with patch.dict('os.environ', {'GH_TOKEN': 'test-only-credential'}):
            request = fetch.attachment_request('https://github.com/user-attachments/files/1/test.zip')
            self.assertEqual(request.get_header('Authorization'), 'Bearer test-only-credential')
            redirected = fetch.CheckedRedirect().redirect_request(
                request, None, 302, 'Found', {}, 'https://objects.githubusercontent.com/test.zip')
            self.assertIsNone(redirected.get_header('Authorization'))
            external = fetch.attachment_request('https://objects.githubusercontent.com/test.zip')
            self.assertIsNone(external.get_header('Authorization'))
            with self.assertRaises(urllib.error.URLError):
                fetch.CheckedRedirect().redirect_request(request, None, 302, 'Found', {}, 'https://example.com/test.zip')

    def test_lifecycle(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            original = cat.CATALOG, cat.REMOVALS
            cat.CATALOG, cat.REMOVALS = root / 'catalog.json', root / 'removals.json'
            try:
                fixture = ROOT / 'library/tests/test_clicks.zip'
                pub.prepare(fixture, 'alice', 42, root / 'dist')
                entry = cat.load(cat.CATALOG, {})['songs'][0]
                self.assertEqual(entry['id'], 'community-42')
                self.assertEqual(entry['local_id'], cat.check_zip(fixture)['id'])
                self.assertTrue(cat.check_zip(root / 'dist/publication.zip')['ok'])
                with self.assertRaises(ValueError):
                    pub.prepare(fixture, 'alice', 42, root / 'dist')
                updated = root / 'v2.zip'
                with zipfile.ZipFile(fixture) as src, zipfile.ZipFile(updated, 'w') as dst:
                    for name in src.namelist():
                        data = src.read(name)
                        if name.endswith('.chart'):
                            data = data.replace(b'VERSION 1', b'VERSION 2')
                        dst.writestr(name, data)
                with self.assertRaises(ValueError):
                    pub.prepare(updated, 'bob', 42, root / 'dist')
                pub.prepare(updated, 'alice', 42, root / 'dist')
                self.assertEqual(cat.load(cat.CATALOG, {})['songs'][0]['id'], 'community-42')
                pub.remove('community-42', 'Test withdrawal', root / 'dist')
                self.assertFalse(cat.load(cat.CATALOG, {})['songs'])
                transfer = json.loads((root / 'dist/transfer.json').read_text())
                self.assertIn('songs/community-42-v1.zip', transfer['delete'])
                self.assertIn('songs/community-42-v2.zip', transfer['delete'])
                with self.assertRaises(ValueError):
                    pub.prepare(updated, 'alice', 42, root / 'dist')
            finally:
                cat.CATALOG, cat.REMOVALS = original

    def test_unlicensed_song_is_noted_privately(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            original = cat.CATALOG, cat.REMOVALS
            cat.CATALOG, cat.REMOVALS = root / 'catalog.json', root / 'removals.json'
            try:
                fixture = ROOT / 'library/tests/test_clicks.zip'
                bare = root / 'bare.zip'
                with zipfile.ZipFile(fixture) as src, zipfile.ZipFile(bare, 'w') as dst:
                    for name in src.namelist():
                        data = src.read(name)
                        if name.endswith('.chart'):
                            data = b''.join(l for l in data.splitlines(True) if not l.startswith(b'LICENSE'))
                        dst.writestr(name, data)
                pub.prepare(bare, 'alice', 7, root / 'dist')
                self.assertEqual(cat.load(cat.CATALOG, {})['songs'][0]['license'], '')
                note = root / 'dist/ledger.json'
                self.assertTrue(note.exists())
                register = root / 'unlicensed.md'
                sys.argv = ['library_ledger.py', str(note), str(register)]
                ledger.main()
                self.assertIn('| #7 | community-7 |', register.read_text())
                pub.prepare(fixture, 'alice', 8, root / 'dist')
                self.assertEqual(cat.load(cat.CATALOG, {})['songs'][-1]['id'], 'community-8')
            finally:
                cat.CATALOG, cat.REMOVALS = original

    def test_editor_submission_identity_and_download_host(self):
        body = 'x\nSent from the online editor by **Alice B.**. \n<!-- editor-submission v1 -->'
        mine = {'author': {'login': 'owner'}, 'body': body}
        self.assertEqual(submitter.submitter_of(mine, 'owner'), 'Alice B.')
        # a stranger copying the marker keeps his own GitHub login
        self.assertEqual(submitter.submitter_of({'author': {'login': 'mallory'}, 'body': body}, 'owner'), 'mallory')
        self.assertEqual(submitter.submitter_of({'author': {'login': 'owner'}, 'body': 'plain'}, 'owner'), 'owner')
        good = urlparse('https://incoming.example/' + 'a' * 32 + '.zip')
        with patch.object(fetch, 'INCOMING', 'incoming.example'):
            self.assertTrue(fetch.ALLOWED(good))
            self.assertFalse(fetch.ALLOWED(urlparse('https://incoming.example/other/' + 'a' * 32 + '.zip')))
            self.assertFalse(fetch.ALLOWED(urlparse('https://evil.example/' + 'a' * 32 + '.zip')))
        with patch.object(fetch, 'INCOMING', ''):
            self.assertFalse(fetch.ALLOWED(good))

    def test_object_urls_are_joined_with_a_slash(self):
        self.assertEqual(site.join_url('https://x.r2.dev', 'thumbs/a.jpg'), 'https://x.r2.dev/thumbs/a.jpg')
        self.assertEqual(site.join_url('/_demo/', 'songs/a.zip'), '/_demo/songs/a.zip')
        self.assertEqual(site.join_url('https://x.r2.dev', None), '')
        self.assertEqual(site.thumb_url({'thumb_base': 'https://x.r2.dev'}, {'thumb': 'thumbs/a.jpg'}), 'https://x.r2.dev/thumbs/a.jpg')


if __name__ == '__main__':
    unittest.main()
