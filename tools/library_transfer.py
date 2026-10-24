#!/usr/bin/env python3
"""Transfer only prepared catalogue objects to R2; never execute supplied files."""
import json
import os
import re
import subprocess
from pathlib import Path

root = Path('library/dist')
manifest = json.loads((root / 'transfer.json').read_text())
bucket = os.environ['R2_BUCKET']
endpoint = os.environ['R2_ENDPOINT']
if not re.fullmatch(r'[a-z0-9][a-z0-9-]{1,61}[a-z0-9]', bucket):
    raise ValueError('Invalid bucket')
if not re.fullmatch(r'https://[a-f0-9]{32}\.r2\.cloudflarestorage\.com', endpoint):
    raise ValueError('Invalid R2 endpoint')
for action in ('upload', 'delete'):
    for key in manifest[action]:
        if not re.fullmatch(r'(songs/[A-Za-z0-9_-]+-v\d+\.zip|thumbs/[A-Za-z0-9_-]+-v\d+\.jpg)', key):
            raise ValueError('Invalid object key')
        base = ['aws', '--endpoint-url', endpoint, 's3']
        if action == 'upload':
            command = base + ['cp', str(root / key), f's3://{bucket}/{key}']
        else:
            command = base + ['rm', f's3://{bucket}/{key}']
        subprocess.run(command, check=True)
