"""PlatformIO pre-build hook: attach Git state and JST build time."""
from pathlib import Path
import sys

Import("env")
project = Path(env.subst("$PROJECT_DIR"))
sys.path.insert(0, str(project / "tools"))
from build_metadata import write_metadata

generated = Path(env.subst("$BUILD_DIR")) / "generated"
info = write_metadata(generated / "BuildMetadata.h", project)
env.Append(CPPPATH=[str(generated)])
print("[BUILD INFO] " + info["git_revision"] +
      ("+dirty" if info["git_dirty"] > 0 else "+unknown" if info["git_dirty"] < 0 else "") +
      " " + info["build_date"] + " " + info["build_time"])
