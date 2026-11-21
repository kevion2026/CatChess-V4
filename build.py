"""Build the frozen CatChess V4 editions using MinGW-w64 GCC."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("edition", choices=["all", "pro", "flash"], default="all", nargs="?")
    parser.add_argument("--cxx", default=os.environ.get("CXX", "g++"))
    args = parser.parse_args()
    compiler = shutil.which(args.cxx)
    if compiler is None:
        parser.error("MinGW-w64 GCC not found. Set CXX or use --cxx.")
    compiler = Path(compiler).resolve()
    root = Path(__file__).resolve().parent
    output = root / "build"
    output.mkdir(exist_ok=True)
    env = {**os.environ, "PATH": str(compiler.parent) + os.pathsep + os.environ.get("PATH", ""),
           "TMP": str(output), "TEMP": str(output)}
    for edition in ("pro", "flash") if args.edition == "all" else (args.edition,):
        source_dir = root / edition
        profile = json.loads((source_dir / "build-profile.json").read_text(encoding="utf-8"))
        executable = output / f"CatChess-V4-{edition}.exe"
        command = [str(compiler), *profile["flags"], profile["source"], "-o", str(executable)]
        subprocess.run(command, cwd=source_dir, env=env, check=True)
        print(f"Built {executable}")


if __name__ == "__main__":
    main()
