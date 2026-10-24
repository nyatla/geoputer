"""Compare RAW row traversal with its frozen scalar predecessor on the host."""
import argparse
import json
from pathlib import Path
import shutil
import statistics
import subprocess
import tempfile

root=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument("--rounds",type=int,default=5)
p.add_argument("--iterations",type=int,default=64)
p.add_argument("--output",type=Path,required=True)
a=p.parse_args()
if a.rounds<1 or a.iterations<1:p.error("rounds and iterations must be positive")
source=root/"tests/galuchat/raw_row_test.cpp"
results={"before":[],"rows":[]}
with tempfile.TemporaryDirectory(prefix="reversegeoputer-raw-compare-") as tmp:
    work=Path(tmp)
    before=work/"before/galuchat"
    shutil.copytree(root/"third_party/galuchat/include/galuchat",before)
    header=before/"wgsmap3_reader.hpp"
    text=header.read_text()
    start=text.index("    template <class Raster>\n    void readRawRect(")
    end=text.index("\n    void skipRaw(",start)
    frozen=(root/"tests/performance/baseline/RawBeforeRows.inc").read_text()
    frozen=frozen[frozen.index("    template <class Raster>"):]
    header.write_text(text[:start]+frozen+text[end:])
    checks={}
    for name,include in [("before",before.parent),("rows",root/"third_party/galuchat/include")]:
        common=["g++","-std=c++17","-Wall","-Wextra","-Werror","-pedantic","-I",str(include),str(source)]
        binary=work/(name+"-check")
        subprocess.run([*common,"-O2","-fsanitize=undefined","-fno-sanitize-recover=all","-D_GLIBCXX_ASSERTIONS","-o",str(binary)],check=True,timeout=90)
        checks[name]=subprocess.check_output([str(binary)],text=True,timeout=60).strip()
        subprocess.run([*common,"-O3","-DNDEBUG","-o",str(work/(name+"-bench"))],check=True,timeout=90)
    if checks["before"]!=checks["rows"]:raise RuntimeError(f"RAW trace mismatch: {checks}")
    for round_number in range(a.rounds):
        for name in (["before","rows"] if round_number%2==0 else ["rows","before"]):
            output=subprocess.check_output([str(work/(name+"-bench")),str(a.iterations)],text=True,timeout=90)
            cases=[json.loads(line) for line in output.splitlines()]
            results[name].append(cases)
            print(f"round={round_number+1} variant={name} total_ms={sum(c['ns'] for c in cases)/1e6:.3f}",flush=True)

summary=[]
for n in range(len(results["before"][0])):
    original=results["before"][0][n]
    identity={k:original[k] for k in ("palette","compression","profile","iterations","sum","checksum")}
    times={}
    for name,rounds in results.items():
        for cases in rounds:
            if any(cases[n][k]!=v for k,v in identity.items()):raise RuntimeError("benchmark image/case mismatch")
        times[name]=statistics.median(cases[n]["ns"] for cases in rounds)
    summary.append(dict(identity,before_ns=times["before"],rows_ns=times["rows"],
                        change_pct=100*(times["rows"]/times["before"]-1)))
record={"scope":"host in-memory RAW decode only, RawRaster, O3, no SD/UI/Wordbook",
        "trace":checks["rows"],"rounds":a.rounds,"cases":summary,"raw":results}
a.output.parent.mkdir(parents=True,exist_ok=True)
a.output.write_text(json.dumps(record,indent=2)+"\n")
print(checks["rows"])
print(f"saved {a.output}; {len(summary)} cases x {a.rounds} rounds per variant")
