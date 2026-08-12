#!/usr/bin/env python3
# Adds a "list <dir>" command to the a314 disk daemon (disk.py).
# Usage: patch_disk.py [target.py]   (default /opt/a314/disk.py)
# Writes the patched result to <target>.new (idempotent).
import sys

target = sys.argv[1] if len(sys.argv) > 1 else '/opt/a314/disk.py'
src = open(target, encoding='utf-8').read()

if 'def list_dir' in src:
    print('already patched: ' + target); sys.exit(0)

# 1) import os
src = src.replace('import json\n', 'import json\nimport os\n', 1)

# 2) list_dir method, inserted before handle_control_socket_readable
method = (
    "    def list_dir(self, path):\n"
    "        if not path:\n"
    "            path = '/'\n"
    "        try:\n"
    "            names = os.listdir(path)\n"
    "        except Exception as e:\n"
    "            return '! ' + str(e)\n"
    "        dirs = []\n"
    "        files = []\n"
    "        for n in sorted(names, key=lambda x: x.lower()):\n"
    "            full = os.path.join(path, n)\n"
    "            try:\n"
    "                if os.path.isdir(full):\n"
    "                    dirs.append('d ' + n)\n"
    "                elif n.lower().endswith(('.adf', '.hdf', '.adz')):\n"
    "                    files.append('f ' + n)\n"
    "            except OSError:\n"
    "                pass\n"
    "        return '\\n'.join(dirs + files)\n"
    "\n"
)
anchor = "    def handle_control_socket_readable(self, s: socket.socket):"
assert anchor in src, 'anchor not found'
src = src.replace(anchor, method + anchor, 1)

# 3) the "list" command branch
old = ("                res = self.insert_adf(unit, fn, rw)\n"
       "            else:\n")
new = ("                res = self.insert_adf(unit, fn, rw)\n"
       "            elif cmd == 'list':\n"
       "                path = ' '.join(arr[1:])\n"
       "                res = self.list_dir(path)\n"
       "            else:\n")
assert old in src, 'insert/else block not found'
src = src.replace(old, new, 1)

out = target + '.new'
open(out, 'w', encoding='utf-8').write(src)
print('patched -> ' + out)
