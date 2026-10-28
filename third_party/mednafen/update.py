#!/usr/bin/env python3
"""Refresh this directory from a Mednafen git checkout: copies the files listed in files.txt at the given revision
and restores the include/mednafen -> ../src link. satz.py applies its text patches to these files at build time."""

import argparse
import io
import os
from pathlib import Path
import subprocess
import tarfile

HERE = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('checkout', type=Path, help='Mednafen git checkout')
    parser.add_argument('--rev', default='HEAD')
    args = parser.parse_args()
    files = (HERE / 'files.txt').read_text().split()
    archive = subprocess.run(['git', '-C', str(args.checkout), 'archive', args.rev, *files],
                             check=True, capture_output=True).stdout
    with tarfile.open(fileobj=io.BytesIO(archive)) as tar:
        tar.extractall(HERE, filter='data')
    link = HERE / 'include/mednafen'
    link.parent.mkdir(exist_ok=True)
    if not link.is_symlink():
        os.symlink('../src', link)
    commit = subprocess.run(['git', '-C', str(args.checkout), 'rev-parse', args.rev], check=True,
                            capture_output=True, text=True).stdout.strip()
    (HERE / 'REVISION').write_text(commit + '\n')
    print(f'{len(files)} files from {commit}')


if __name__ == '__main__':
    main()
