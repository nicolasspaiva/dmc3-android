"""List the contents of a .nbz (ZIP) game archive by folder and extension."""
import collections
import os
import sys
import zipfile

z = zipfile.ZipFile(sys.argv[1])
il = z.infolist()
print("entries", len(il), "uncompressed GB", round(sum(i.file_size for i in il) / 1e9, 2),
      "compressed GB", round(sum(i.compress_size for i in il) / 1e9, 2))
print("methods", collections.Counter(i.compress_type for i in il))
top, ext, sz = collections.Counter(), collections.Counter(), collections.Counter()
for i in il:
    p = i.filename.replace("\\", "/")
    parts = p.split("/")
    top["/".join(parts[:2]) if len(parts) > 2 else parts[0]] += 1
    e = os.path.splitext(p)[1].lower()
    ext[e] += 1
    sz[e] += i.file_size
print("top dirs", top.most_common(30))
print("by ext (count, MB)", [(e, ext[e], round(sz[e] / 1e6)) for e, _ in sz.most_common(15)])
print("sample", [i.filename for i in il[:15]])
