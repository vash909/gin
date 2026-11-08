"""Exercise staged install/uninstall with either GNU make or bmake."""
import os
from pathlib import Path
import stat
import subprocess
import sys
import tempfile

repo = Path(__file__).resolve().parent.parent
make = sys.argv[1] if len(sys.argv) > 1 else "make"
scratch = Path(os.environ.get("TMPDIR", str(repo / ".build/test-scratch")))
scratch.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix="gin-install-", dir=scratch) as directory:
    stage = Path(directory) / "stage with spaces"
    prefix = "/opt/gin test"
    args = [make, "DESTDIR=" + str(stage), "PREFIX=" + prefix]
    subprocess.run(args + ["install"], cwd=repo, check=True)
    installed = stage / prefix.lstrip("/")
    binary = installed / "bin/gin"
    manual = installed / "share/man/man1/gin.1"
    assert binary.read_bytes() == (repo / ".build/gin").read_bytes()
    assert manual.read_bytes() == (repo / "man/gin.1").read_bytes()
    assert stat.S_IMODE(binary.stat().st_mode) == 0o755
    assert stat.S_IMODE(manual.stat().st_mode) == 0o644
    result = subprocess.run([str(binary), "--version"], check=True,
                            capture_output=True, text=True)
    assert result.stdout == "gin 0.1.0-dev\n"
    sentinel = installed / "bin/keep-me"
    sentinel.write_text("unrelated file\n", encoding="utf-8")
    subprocess.run(args + ["uninstall"], cwd=repo, check=True)
    assert not binary.exists()
    assert not manual.exists()
    assert sentinel.read_text(encoding="utf-8") == "unrelated file\n"
print(make + ": staged install, version, modes, uninstall, unrelated-file preservation OK")
