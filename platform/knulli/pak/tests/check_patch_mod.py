# nebula-pak --patch-mod's test: every file of the mod folder is the patched game.pak's file of that path.
#   check_patch_mod.py <mod folder> <patched game.pak>
import os
import struct
import sys

mod, pak = sys.argv[1], open(sys.argv[2], "rb")
boot = pak.read(0x440)
pak.seek(struct.unpack(">I", boot[0x424:0x428])[0] << 2)
table = pak.read(struct.unpack(">I", boot[0x428:0x42C])[0] << 2)
count = struct.unpack(">I", table[8:12])[0]
files, dirs = {}, []
for i in range(1, count):
    while dirs and i >= dirs[-1][0]:
        dirs.pop()
    entry = table[i * 12:i * 12 + 12]
    at = count * 12 + (struct.unpack(">I", entry[:4])[0] & 0xFFFFFF)
    path = (dirs[-1][1] + "/" if dirs else "") + table[at:table.index(b"\0", at)].decode()
    if entry[0] == 1:
        dirs.append((struct.unpack(">I", entry[8:12])[0], path))
    else:
        files[path] = (struct.unpack(">I", entry[4:8])[0] << 2, struct.unpack(">I", entry[8:12])[0])
checked = 0
for root, _, names in os.walk(os.path.join(mod, "files")):
    for name in names:
        relative = os.path.relpath(os.path.join(root, name), os.path.join(mod, "files"))
        offset, size = files[relative]
        pak.seek(offset)
        if open(os.path.join(root, name), "rb").read() != pak.read(size):
            print("distinto:", relative)
            sys.exit(1)
        checked += 1
sys.exit(0 if checked else 1)
