"""Run the original grader while retaining every QEMU session, including passes."""
from pathlib import Path
import runpy
import subprocess
import sys

root = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(root))
subprocess.run(["make", "clean"], check=True)
import gradelib

original = gradelib.Runner.run_qemu
counter = 0


def record(self, *args, **kwargs):
    global counter
    counter += 1
    path = Path("ci-transcripts") / f"qemu-{counter:02d}.log"
    try:
        return original(self, *args, **kwargs)
    finally:
        path.parent.mkdir(exist_ok=True)
        if hasattr(self, "qemu"):
            path.write_text(self.qemu.output)
            print(f"Full QEMU transcript: {path}")


gradelib.Runner.run_qemu = record
sys.argv[0] = "grade-lab-lock"
runpy.run_path("grade-lab-lock", run_name="__main__")
