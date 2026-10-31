"""Shared preparation for isolated firmware experiments; never patch production."""
from pathlib import Path
import re
import shutil


def replace_once(source, old, new):
    if source.count(old) != 1:
        raise RuntimeError(f"Benchmark anchor changed or ambiguous: {old[:90]!r}")
    return source.replace(old, new, 1)


def prepare_workspace(root, name, headers):
    if not re.fullmatch(r"[a-z0-9-]+", name):
        raise ValueError("Invalid benchmark directory name")
    work = root / ".pio" / name
    work.mkdir(parents=True, exist_ok=True)
    app = work / "application"
    # Repeated preparations must not retain old scalar/RAW/span comparison TUs.
    if app.is_symlink():
        raise RuntimeError("Benchmark source directory must not be a symlink")
    if app.exists():
        shutil.rmtree(app)
    shutil.copytree(root / "application", app)
    bench = app / "benchmark"
    bench.mkdir()
    for header in headers:
        shutil.copyfile(root / "tests/performance" / header, bench / Path(header).name)
    return work


def inject_storage_probe(source, header, body):
    source = replace_once(source, '#include "GpsStorageTask.h"',
                          f'#include "GpsStorageTask.h"\n#include "../../benchmark/{header}"')
    start = source.index("void GpsStorageTask::run()")
    before, loop = source[:start], source[start:]
    marker = "        vTaskDelay(pdMS_TO_TICKS(5));"
    return before + replace_once(loop, marker, body + marker)


def diagnostic_config(root, work):
    name = work.name
    source = replace_once((root / 'platformio.ini').read_text(), 'src_dir = application',
                          f'src_dir = .pio/{name}/application\nbuild_dir = .pio/{name}/build\nlibdeps_dir = .pio/libdeps')
    return replace_once(source, '    -Wall', '    -Wall\n    -DREVERSEGEOPUTER_DIAGNOSTICS=1')
