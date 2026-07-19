#!/usr/bin/env python3
from __future__ import annotations
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LINK = re.compile(r"\[[^\]]*\]\(([^)]+)\)")
missing: list[tuple[Path, str]] = []
for doc in ROOT.rglob("*.md"):
    if any(p in {"build", ".git", "third_party"} for p in doc.parts):
        continue
    text = doc.read_text(encoding="utf-8", errors="replace")
    for raw in LINK.findall(text):
        target = raw.strip().split()[0].strip("<>")
        if not target or target.startswith(("http://", "https://", "mailto:", "#")):
            continue
        path_part = target.split("#", 1)[0]
        if path_part and not (doc.parent / path_part).resolve().exists():
            missing.append((doc.relative_to(ROOT), target))
if missing:
    for doc, target in missing:
        print(f"{doc}: missing {target}")
    raise SystemExit(1)
print("Markdown links: OK")
