"""Create immutable firmware build metadata without modifying source files."""
from datetime import datetime, timedelta, timezone
import json
import os
from pathlib import Path
import re
import subprocess

JST = timezone(timedelta(hours=9), "JST")


def collect_metadata(root, now=None):
    stamp = (now or datetime.now(JST)).astimezone(JST)
    revision, dirty = "unknown", -1
    def git(*args):
        return subprocess.run(["git", "-C", str(root), *args], check=True, text=True,
                              stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                              env=dict(os.environ, GIT_OPTIONAL_LOCKS="0"), timeout=30).stdout
    try:
        candidate = git("rev-parse", "--short=8", "HEAD").strip()
        if re.fullmatch(r"[0-9a-fA-F]{8,40}", candidate):
            revision = candidate
            dirty = int(bool(git("status", "--porcelain=v1", "--untracked-files=normal").strip()))
    except (OSError, subprocess.CalledProcessError, subprocess.TimeoutExpired):
        pass
    return {"build_date": stamp.strftime("%Y-%m-%d"),
            "build_time": stamp.strftime("%H:%M:%S JST"),
            "git_revision": revision, "git_dirty": dirty}


def write_metadata(path, root, now=None):
    values = collect_metadata(root, now)
    text = "// Generated at build time; do not edit.\n#pragma once\n"
    for key, value in values.items():
        text += "#define REVERSEGEOPUTER_" + key.upper() + " " + json.dumps(value) + "\n"
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or path.read_text() != text:
        path.write_text(text)
    return values
