"""Run the shared explicit-resource Reader regression suite."""
import runpy
from pathlib import Path
runpy.run_path(str(Path(__file__).with_name("run_observed_reader.py")),run_name="__main__")
