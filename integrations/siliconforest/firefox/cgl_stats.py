# SPDX-License-Identifier: MIT
"""Read cache counters from the parent's already-observed owned CGL image."""
import json
from pathlib import PurePosixPath


def inspect(client, providers, output):
    parent = next(row for row in providers['processes'] if row['role'] == 'parent')
    paths = [line.rsplit('> ', 1)[-1] for line in parent['images'] if ' OpenGL ' in line]
    runtime = PurePosixPath('/usr/local/lib/siliconforest/firefox/160-system1')
    path = PurePosixPath(paths[0]) if len(paths) == 1 else None
    if (path is None or '..' in path.parts or not (
            path.is_relative_to('/private/var/db/sffx') or
            path == runtime / 'gl/Frameworks/OpenGL.framework/Versions/A/OpenGL' or
            (path.is_relative_to(runtime / 'overrides') and str(path).endswith('/gl/Frameworks/OpenGL.framework/Versions/A/OpenGL')) or
            path == PurePosixPath('/System/Library/Frameworks/OpenGL.framework/Versions/A/OpenGL'))):
        raise RuntimeError('Expected one already-loaded owned CGL image')
    client.command('Marionette:SetContext', {'value': 'chrome'})
    try:
        result = client.command('WebDriver:ExecuteScript', dict(script='''const {ctypes}=
          ChromeUtils.importESModule("resource://gre/modules/ctypes.sys.mjs");
          const lib=ctypes.open(arguments[0]);
          try { const values=ctypes.uint64_t.array(3)();
            const get=lib.declare("CGLSFFXExportCacheStats",ctypes.default_abi,
              ctypes.void_t,ctypes.uint64_t.ptr);
            get(values);return {hits:Number(values[0].toString()),
              misses:Number(values[1].toString()),evictions:Number(values[2].toString())};
          } finally {lib.close();}''', args=[paths[0]], newSandbox=False))
    finally:
        client.command('Marionette:SetContext', {'value': 'content'})
    result.update(provider=paths[0], complete=result['hits'] > 0)
    output.write_text(json.dumps(result, indent=2)+'\n')
    return result
