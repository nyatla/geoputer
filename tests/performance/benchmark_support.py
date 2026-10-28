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
    source = replace_once(source, '#include "StorageTask.h"',
                          f'#include "StorageTask.h"\n#include "../../benchmark/{header}"')
    start = source.index("void StorageTask::run()")
    before, loop = source[:start], source[start:]
    marker = "        vTaskDelay(pdMS_TO_TICKS(5));"
    return before + replace_once(loop, marker, body + marker)


def instrument_decode_reader(work):
    path = work / "application/task/storage/galuchat/DiskIndexedMapReader.cpp"
    source = replace_once(path.read_text(), '#include "DiskIndexedMapReader.h"',
                          '#include "DiskIndexedMapReader.h"\n#include "../../../benchmark/DecodeProfile.h"')
    # Current whole-request collection closes the index before decoding.
    # Also support saved per-chunk and original readers used by comparisons.
    if 'r.number, r.count, r.length' in source:
        source = replace_once(source, '            chunk.readRectWithoutTail(x, y, view);',
                              '            { benchmark::DecodeScope decode_scope; chunk.readRectWithoutTail(x, y, view); }')
        call = '            galuchat::GaluchatImageDataChunk01Reader::readRectFromBlockRange('
        end = '*stream, {int(r.width), int(r.height), int(r.square), r.number, r.count, r.length}, x, y, view);'
    else:
        source = replace_once(source, '            auto stream = map.createRange(start, bytes);',
                              '            benchmark::DecodeScope decode_scope;\n            auto stream = map.createRange(start, bytes);')
        if 'g.number, g.count, g.length' in source:
            call = '        galuchat::GaluchatImageDataChunk01Reader::readRectFromBlockRange('
            end = '*stream, {int(width), int(height), int(square), g.number, g.count, g.length}, x, y, view);'
        else:
            call = '                galuchat::GaluchatImageDataChunk01Reader::readRectFromBlockRange('
            end = '*stream, {int(width), int(height), int(square), number, count, length}, x, y, view);'
    source = replace_once(source, call, '        { benchmark::DecodeScope decode_scope;\n' + call)
    source = replace_once(source, end, end + ' }')
    path.write_text(source)
    return path, source


def install_background_latch(work, variable, definition_header=None):
    if definition_header:
        header = work / "application/benchmark" / definition_header
        source = replace_once(header.read_text(), '#pragma once', '#pragma once\n#include <atomic>')
        source = replace_once(source, 'namespace geoputer::benchmark {',
                              f'namespace geoputer::benchmark {{\ninline std::atomic<uint32_t> {variable}{{0}};')
        header.write_text(source)
    ui = work / "application/task/ui/UiTask.cpp"
    source = replace_once(ui.read_text(), '#include "UiTask.h"',
                          f'#include "UiTask.h"\n#include <atomic>\nnamespace geoputer::benchmark {{ extern std::atomic<uint32_t> {variable}; }}')
    marker = '        if (ready && model_.takeRedraw()) renderer.draw(model_);'
    source = replace_once(source, marker, marker + f'''
        if(ready && model_.mode()==DisplayMode::Background) {{
            if(!benchmark::{variable}.load(std::memory_order_relaxed))
                benchmark::{variable}.store(now,std::memory_order_relaxed);
        }} else benchmark::{variable}.store(0,std::memory_order_relaxed);
''')
    ui.write_text(source)


def diagnostic_config(root, work):
    name = work.name
    source = replace_once((root / 'platformio.ini').read_text(), 'src_dir = application',
                          f'src_dir = .pio/{name}/application\nbuild_dir = .pio/{name}/build\nlibdeps_dir = .pio/libdeps')
    return replace_once(source, '    -Wall', '    -Wall\n    -DREVERSEGEOPUTER_DIAGNOSTICS=1')


def copy_device_helpers(root, work):
    # Device tools/dependencies are local and optional. Preparing source must
    # also work on a clean checkout without artifacts from a prior experiment.
    for name in ('flash.py', 'capture.py', 'run_device.py', 'run_job.py'):
        for folder in ('navigation-checks', 'raw-device', 'map-heap'):
            source = root / '.pio' / folder / name
            if source.is_file():
                shutil.copyfile(source, work / name)
                break
