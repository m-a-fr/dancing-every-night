#!/usr/bin/env python3
"""Prepare a publication locally. Network upload is a separate workflow step."""
import argparse
import hashlib
import json
import re
import zipfile
from pathlib import Path

import catalog as cat


def prepare(source, submitter, issue, out):
    rep = cat.check_zip(source)
    if not rep['ok']:
        raise ValueError('The song does not pass the game validator')
    for field in ('id', 'title', 'mapper', 'license'):
        if not rep[field]:
            raise ValueError(f'Missing {field}')
    songs = cat.load(cat.CATALOG, {'songs': []})['songs']
    # One proposal is one stable publication identity, including later revisions.
    old = next((s for s in songs if s.get('issue') == issue), None)
    identity = old['id'] if old else f'community-{issue}'
    if old and old.get('submitter', '').lower() != submitter.lower():
        raise ValueError('Only the original submitter can update this song')
    if old and rep['version'] <= old['version']:
        raise ValueError('An update requires a higher VERSION')
    if any(s['id'] == identity and s.get('issue') != issue for s in songs):
        raise ValueError('Publication identity already reserved')
    # Removed IDs remain reserved; restoring requires an explicit maintainer decision.
    if any(s['id'] == identity for s in cat.load(cat.REMOVALS, {'removals': []})['removals']):
        raise ValueError('This publication was removed; open a new proposal')
    out.mkdir(parents=True, exist_ok=True)
    rewritten = out / 'publication.zip'
    with zipfile.ZipFile(source) as src, zipfile.ZipFile(rewritten, 'w', zipfile.ZIP_DEFLATED) as dst:
        for item in src.infolist():
            if item.is_dir():
                continue
            data = src.read(item)
            name = Path(item.filename).name
            if name.endswith('.chart'):
                text = data.decode('utf-8-sig')
                text, count = re.subn(r'^ID\s+[^\r\n]+', f'ID {identity}', text, flags=re.M)
                if count != 1:
                    raise ValueError('Expected exactly one ID per difficulty')
                data = text.encode('utf-8')
            dst.writestr(f'{identity}/{name}', data)
    checked = cat.check_zip(rewritten)
    if not checked['ok'] or checked['id'] != identity:
        raise ValueError('Rewritten package failed validation')
    args = argparse.Namespace(zip=str(rewritten), out=str(out), submitter=submitter, issue=issue, force=False)
    cat.cmd_add(args)
    catalog = cat.load(cat.CATALOG, {})
    entry = next(s for s in catalog['songs'] if s['id'] == identity)
    entry['local_id'] = old.get('local_id', rep['id']) if old else rep['id']
    entry['previous_objects'] = (old.get('previous_objects', []) + [old['zip']] + ([old['thumb']] if old.get('thumb') else [])) if old else []
    entry['source_sha256'] = hashlib.sha256(Path(source).read_bytes()).hexdigest()
    cat.save(cat.CATALOG, catalog)
    keys = [entry['zip']] + ([entry['thumb']] if entry.get('thumb') else [])
    (out / 'transfer.json').write_text(json.dumps({'upload': keys, 'delete': []}))
    # Keep the relation explicit. Records are not silently merged for unrelated local IDs.
    print(f"Published identity: {identity}; original local identity: {entry['local_id']}")


def remove(identity, reason, out):
    old = next((s for s in cat.load(cat.CATALOG, {'songs': []})['songs'] if s['id'] == identity), None)
    if not old:
        raise ValueError('Song not found')
    out.mkdir(parents=True, exist_ok=True)
    cat.cmd_remove(argparse.Namespace(id=identity, reason=reason))
    keys = old.get('previous_objects', []) + [old['zip']] + ([old['thumb']] if old.get('thumb') else [])
    (out / 'transfer.json').write_text(json.dumps({'upload': [], 'delete': keys}))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--zip')
    ap.add_argument('--submitter')
    ap.add_argument('--issue', type=int)
    ap.add_argument('--remove')
    ap.add_argument('--reason', default='')
    ap.add_argument('--out', type=Path, default=Path('library/dist'))
    a = ap.parse_args()
    if a.remove:
        if not a.reason.strip():
            ap.error('A removal reason is required')
        remove(a.remove, a.reason, a.out)
    else:
        if not a.zip or not a.submitter or not a.issue or a.issue <= 0:
            ap.error('--zip, --submitter and a positive --issue are required')
        prepare(Path(a.zip), a.submitter, a.issue, a.out)


if __name__ == '__main__':
    main()
