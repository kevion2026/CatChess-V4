"""Check that both V4 editions respond to UCI and return a move."""
import re
from pathlib import Path
import subprocess


root = Path(__file__).resolve().parent
for edition in ("pro", "flash"):
    executable = root / "build" / f"CatChess-V4-{edition}.exe"
    commands = b"uci\nisready\nposition startpos\ngo movetime 100\nquit\n"
    result = subprocess.run([str(executable)], input=commands, capture_output=True, timeout=20, check=True)
    lines = result.stdout.splitlines()
    if b"uciok" not in lines or b"readyok" not in lines:
        raise RuntimeError(f"UCI handshake failed for {edition}")
    if not any(re.fullmatch(rb"bestmove [a-i][0-9][a-i][0-9](?: .*)?", line) for line in lines):
        raise RuntimeError(f"No opening move returned for {edition}")
    print(f"{edition}: UCI handshake and opening search passed")
