# SPDX-License-Identifier: MIT
"""Bounded stdout collection from this diagnostic's new HOME only."""
import hashlib
from pathlib import Path
import re

MAX_BYTES=8_000_000


def collect(run,home):
    run=Path(run).resolve();home=Path(home)
    if home!=run/'home' or home.is_symlink():raise ValueError('Only this run\'s fresh HOME may be collected')
    source=home/'Library/Logs/SiliconForest/Firefox'
    if not source.exists():return []
    if not source.resolve().is_relative_to(home):raise ValueError('Log directory escapes own HOME')
    files=sorted(source.glob('firefox-*.log'))
    if len(files)>4:raise ValueError('Unexpected number of own launcher logs')
    target=run/'native-output';target.mkdir()
    rows=[]
    for path in files:
        if path.is_symlink() or not path.is_file() or not re.fullmatch(r'firefox-\d+-\d+\.log',path.name):
            raise ValueError('Unexpected native log path')
        with path.open('rb') as stream:data=stream.read(MAX_BYTES+1)
        if len(data)>MAX_BYTES:raise ValueError('Native log exceeds collection bound')
        output=target/path.name;output.write_bytes(data)
        rows.append(dict(path=str(output.relative_to(run)),bytes=len(data),sha256=hashlib.sha256(data).hexdigest()))
    return rows
