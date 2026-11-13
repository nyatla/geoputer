"""Regenerate current Live Place and bilingual Settings / log browser images from production code."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]

if __name__ == "__main__":
    for generator in ("render_live_place_layout.py", "render_settings_layout.py", "render_log_browser.py"):
        subprocess.run([sys.executable, str(ROOT / "tools" / generator)], check=True)
