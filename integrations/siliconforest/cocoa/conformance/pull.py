#!/usr/bin/env python3
"""Copy files out of a run's disk image (kept with `cocoa_check.sh ... --keep-image`).

  pull.py runs/cocoa/<run>/disk.img /private/tmp/render-controls.png=out.png [GUEST=HOST ...]

Nothing is mounted: debugfs reads the root partition at its offset, as cocoa_image.py writes it.
"""
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'qemu-dev'))
import qd_image  # noqa: E402


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    image = sys.argv[1]
    offset, _ = qd_image.partitions(image)[1]
    failed = 0
    for pair in sys.argv[2:]:
        guest, host = pair.split('=', 1)
        subprocess.run(['debugfs', '-R', 'dump "%s" "%s"' % (guest, host), '%s?offset=%d' % (image, offset)],
                       capture_output=True, text=True)
        size = Path(host).stat().st_size if Path(host).exists() else 0
        print('%s -> %s (%d bytes)' % (guest, host, size))
        failed += size == 0
    sys.exit(1 if failed else 0)


if __name__ == '__main__':
    main()
