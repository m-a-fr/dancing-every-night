#!/usr/bin/env python3
"""Snapshot the latest ZIP from the proposal author, before privileged transfer."""
import hashlib
import json
import os
import subprocess
import sys

import library_fetch

proposal = json.load(open('proposal.json'))
if 'song-submission' not in [label['name'] for label in proposal['labels']]:
    sys.exit('Not a song proposal')
proposal['comments'] = [dict(body=c['body'], author={'login': c['user']['login']}, updatedAt=c['updated_at']) for page in json.load(open('proposal-comments.json')) for c in page]
author = proposal['author']['login']
texts = [proposal['body']] + [c['body'] for c in proposal['comments'] if c['author']['login'] == author]
text = next((t for t in reversed(texts) if library_fetch.URL.search(t)), '')
subprocess.run([sys.executable, 'tools/library_fetch.py', 'submission.zip'], env={**os.environ, 'TEXT': text}, check=True)
digest = hashlib.sha256(open('submission.zip', 'rb').read()).hexdigest()
reports = [c for c in proposal['comments'] if c['author']['login'] in ('github-actions[bot]', 'github-actions') and 'Source SHA-256:' in c['body']]
if not reports or f'Source SHA-256: `{digest}`' not in reports[-1]['body'] or '✅ Ready for review' not in reports[-1]['body']:
    sys.exit('The current ZIP needs a matching successful automatic report before publication')
if os.environ.get('APPROVED_AT') and reports[-1]['updatedAt'] > os.environ['APPROVED_AT']:
    sys.exit('The report changed after approval; review the new report and reapply approved')
print('Source SHA-256: `' + digest + '`')
