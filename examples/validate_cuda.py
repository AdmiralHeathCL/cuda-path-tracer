"""Run mandatory device validation; missing hardware returns 77, never success."""

import argparse
from pathlib import Path
import shutil
import subprocess
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--build", type=Path, default=Path("build"))
parser.add_argument("--sanitizer", action="store_true", help="also run Compute Sanitizer memory, initialization, race and synchronization checks")
parser.add_argument("--obj", type=Path, help="also compare ray hits/rendering on this OBJ")
args = parser.parse_args()
commands = []
for name in ["cuda_tests", "cuda_lighting_tests"]:
    exe = (args.build / name).resolve()
    if not exe.is_file():
        parser.error(f"{name} is missing; configure CUDA with BUILD_TESTING=ON and build first")
    commands.append([str(exe)])
for command in commands:
    result = subprocess.run(command)
    if result.returncode == 77:
        print("GPU validation did not run: use a session with working CUDA device access.", file=sys.stderr)
        sys.exit(77)
    if result.returncode:
        sys.exit(result.returncode)
if args.obj:
    result = subprocess.run(commands[0] + [str(args.obj.resolve())])
    if result.returncode:
        sys.exit(result.returncode)
if args.sanitizer:
    tool = shutil.which("compute-sanitizer")
    if not tool:
        parser.error("Compute Sanitizer is not on PATH")
    for check in ["memcheck", "initcheck", "racecheck", "synccheck"]:
        for command in commands:
            result = subprocess.run([tool, "--tool", check, "--error-exitcode", "1"] + command)
            if result.returncode:
                sys.exit(result.returncode)
print("CUDA geometry, lighting and pixel-packing comparisons passed" + ("; memory, initialization, race and synchronization checks passed" if args.sanitizer else ""))
