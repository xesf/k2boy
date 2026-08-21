#!/usr/bin/env python3
"""Generate web/data-index.txt — the VFS manifest for the browser runtime.

The native VFS (src/platform/vfs.cpp) walks the install with opendir at mount
time. A browser cannot list a directory over HTTP, so the same index has to be
produced ahead of time and shipped as a file.

Output is one relative path per line, exactly as it exists on disk (original
case preserved). Case folding and backslash normalisation happen in the browser,
in js/core/vfs.js, mirroring vfs.cpp::normalise.

Usage:  python3 tools/webindex.py [data_dir] [out_file]
"""

import os
import sys

SKIP_DIRS = {".git", "__MACOSX"}
SKIP_FILES = {".DS_Store"}

# The install is 949 MB and 6.5k files, but the browser only ever fetches
# geometry, levels, textures and character descriptors. Movies and the original
# executables would triple the manifest for nothing.
SKIP_EXT = {".mov", ".exe", ".dll", ".icd", ".vxd", ".isu", ".w32"}


def main() -> int:
    data = sys.argv[1] if len(sys.argv) > 1 else "data"
    out = sys.argv[2] if len(sys.argv) > 2 else "web/data-index.txt"

    if not os.path.isdir(data):
        sys.stderr.write("webindex: %s is not a directory\n" % data)
        return 1

    paths = []
    for root, dirs, files in os.walk(data):
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
        for name in files:
            if name in SKIP_FILES:
                continue
            if os.path.splitext(name)[1].lower() in SKIP_EXT:
                continue
            rel = os.path.relpath(os.path.join(root, name), data)
            paths.append(rel.replace(os.sep, "/"))

    paths.sort(key=str.lower)

    os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
    with open(out, "w", encoding="utf-8") as f:
        f.write("\n".join(paths))
        f.write("\n")

    size = os.path.getsize(out)
    print("webindex: %d files -> %s (%.1f KB)" % (len(paths), out, size / 1024.0))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
