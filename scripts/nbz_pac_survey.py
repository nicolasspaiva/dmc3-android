"""Group .pac files in a .nbz archive by name prefix and header magic."""
import collections
import re
import sys
import zipfile

z = zipfile.ZipFile(sys.argv[1])
groups = collections.defaultdict(lambda: [0, 0])
magics = collections.Counter()
examples = {}
for i in z.infolist():
    n = i.filename.split("/")[-1]
    if not n.lower().endswith(".pac"):
        continue
    prefix = re.match(r"[a-z_]+", n.lower())
    key = prefix.group(0) if prefix else "?"
    groups[key][0] += 1
    groups[key][1] += i.file_size
    with z.open(i) as f:
        m = f.read(4)
    magic = m.decode("latin-1").rstrip("\0") if all(32 <= c < 127 or c == 0 for c in m) else m.hex()
    magics[magic] += 1
    examples.setdefault(key, n)
for k, (c, s) in sorted(groups.items(), key=lambda x: -x[1][1]):
    print(f"{k:12} {c:5} files {s/1e6:8.1f} MB  e.g. {examples[k]}")
print("magics", magics.most_common(10))
