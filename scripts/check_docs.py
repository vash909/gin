"""Check local Markdown links and the numbered beta roadmap."""
import re
from pathlib import Path

root = Path(__file__).resolve().parent.parent
errors = []
for path in root.rglob("*.md"):
    if any(part in {".git", ".build", ".tooling"} for part in path.parts):
        continue
    for target in re.findall(r"\[[^\]]*\]\(([^\s)]+)(?:[^)]*)\)", path.read_text()):
        if ":" in target or target.startswith("#"):
            continue
        destination = (path.parent / target.split("#", 1)[0]).resolve()
        if not destination.exists():
            errors.append(f"{path.relative_to(root)}: missing link target {target}")
roadmap = (root / "ROADMAP.md").read_text()
numbers = [int(n) for n in re.findall(r"^(\d+)\. ", roadmap, re.MULTILINE)]
if numbers != list(range(1, 21)):
    errors.append(f"Expected exactly roadmap items 1 through 20, found {numbers}")
if errors:
    raise SystemExit("\n".join(errors))
print("Documentation links and all 20 beta roadmap items passed.")
