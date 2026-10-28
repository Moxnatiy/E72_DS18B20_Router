"""Turn SDK-linked files of a CCS project into local copies so they can be patched without touching the SDK.

Usage: python3 localize.py <project_dir> <sdk_dir> <file> [<file> ...]
<file> is the project-relative path as shown in .project (e.g. Application/zcl_genericapp.c).
"""
import re, shutil, sys
from pathlib import Path

proj, sdk, files = Path(sys.argv[1]), Path(sys.argv[2]), sys.argv[3:]
dot_project = proj / ".project"
text = dot_project.read_text()

for name in files:
    m = re.search(r"\s*<link>\s*<name>" + re.escape(name) + r"</name>.*?<locationURI>(.*?)</locationURI>.*?</link>",
                  text, re.S)
    if not m:
        print(f"skip (not linked): {name}")
        continue
    src = sdk / m.group(1).split("/", 1)[1]  # strip COM_TI_..._INSTALL_DIR
    dst = proj / name
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)
    text = text.replace(m.group(0), "", 1)
    print(f"localized: {name} <- {src.relative_to(sdk)}")

dot_project.write_text(text)
