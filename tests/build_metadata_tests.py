"""Exercise actual Git state, JST timestamps and firmware metadata headers."""
from datetime import datetime, timezone
from pathlib import Path
import os
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from build_metadata import collect_metadata, write_metadata

STAMP = datetime(2026, 10, 17, 15, 4, 5, tzinfo=timezone.utc)


def git(repo, *args):
    return subprocess.run(["git", "-C", str(repo), "-c", "core.hooksPath=/dev/null",
                           "-c", "commit.gpgsign=false", *args], check=True, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          env=dict(os.environ, GIT_OPTIONAL_LOCKS="0")).stdout.strip()


with tempfile.TemporaryDirectory(prefix="reversegeoputer-build-info-") as folder:
    work = Path(folder)
    archive = work / "archive"
    archive.mkdir()
    unknown = collect_metadata(archive, STAMP)
    assert unknown == {"build_date": "2026-10-18", "build_time": "00:04:05 JST",
                       "git_revision": "unknown", "git_dirty": -1}
    repo = work / "repo"
    repo.mkdir()
    git(repo, "init", "-q")
    assert collect_metadata(repo, STAMP)["git_dirty"] == -1  # No HEAD yet.
    (repo / ".gitignore").write_text(".pio/\n")
    tracked = repo / "source.txt"
    tracked.write_text("committed\n")
    git(repo, "add", ".gitignore", "source.txt")
    git(repo, "-c", "user.name=Metadata test", "-c", "user.email=test@example.invalid",
        "commit", "-qm", "fixture")
    revision = git(repo, "rev-parse", "--short=8", "HEAD")
    clean = collect_metadata(repo, STAMP)
    assert clean["git_revision"] == revision and clean["git_dirty"] == 0
    header = repo / ".pio/generated/BuildMetadata.h"
    write_metadata(header, repo, STAMP)
    first_mtime = header.stat().st_mtime_ns
    write_metadata(header, repo, STAMP)
    assert header.stat().st_mtime_ns == first_mtime
    assert collect_metadata(repo, STAMP)["git_dirty"] == 0  # Build output is ignored.
    tracked.write_text("local change\n")
    assert collect_metadata(repo, STAMP)["git_dirty"] == 1
    tracked.write_text("committed\n")
    fresh = repo / "new-source.cpp"
    fresh.write_text("// untracked source\n")
    dirty = write_metadata(header, repo, STAMP)
    assert dirty["git_revision"] == revision and dirty["git_dirty"] == 1
    source = work / "metadata.cpp"
    source.write_text('#include "BuildInfo.h"\n#include <cassert>\n#include <cstring>\n'
                      'int main() { assert(geoputer::BuildGitDirty == 1); '
                      'assert(!std::strcmp(geoputer::buildGitSuffix(), "+dirty")); '
                      'assert(!std::strcmp(geoputer::BuildDate, "2026-10-18")); '
                      'assert(!std::strcmp(geoputer::BuildTime, "00:04:05 JST")); '
                      'assert(!std::strcmp(geoputer::BuildGitRevision, "' + revision + '")); }\n')
    binary = work / "metadata-test"
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(header.parent),
                    "-I", str(ROOT / "application"), str(source), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
    write_metadata(header, archive, STAMP)
    source.write_text('#include "BuildInfo.h"\n#include <cassert>\n#include <cstring>\n'
                      'int main() { assert(geoputer::BuildGitDirty == -1); '
                      'assert(!std::strcmp(geoputer::BuildGitRevision, "unknown")); '
                      'assert(!std::strcmp(geoputer::buildGitSuffix(), "")); }\n')
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(header.parent),
                    "-I", str(ROOT / "application"), str(source), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
    fresh.unlink()
    assert collect_metadata(repo, STAMP)["git_dirty"] == 0
print("PASS: build metadata JST/date boundary, Git clean/dirty/untracked/unknown and generated C++ header")
