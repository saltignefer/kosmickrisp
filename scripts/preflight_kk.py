#!/usr/bin/env python3
"""Deterministic hygiene gate, dual-arch build, and install helper for saltignefer/kosmickrisp.

Enforces Mesa upstream AI rules (Generated-by: LLM trailer, forbidden driver directories)
and fork privacy/build invariants so contributor agents cannot ship broken or leaking commits.
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FORBIDDEN_DIRS = (
    "src/asahi/",
    "src/gallium/drivers/asahi/",
    "src/gallium/drivers/nouveau/",
    "src/gallium/drivers/panfrost/",
    "src/imagination/",
    "src/intel/compiler/jay/",
    "src/nouveau/",
    "src/panfrost/",
    "src/poly/",
)
PRIVACY_PATTERNS = [
    re.compile(r"[A-Za-z0-9._%+-]+@(?:gmail|proton|icloud|outlook|hotmail|yahoo)\.[A-Za-z]{2,}", re.I),
    re.compile(r"/Users/[A-Za-z0-9._-]+/"),
    re.compile(r"C:\\\\Users\\\\[A-Za-z0-9._-]+\\\\", re.I),
]
AI_TRAILER_RE = re.compile(r"^(Generated-by|Assisted-by):\s+LLM\b", re.M)


def run(cmd: list[str], *, env: dict[str, str] | None = None, cwd: Path | None = None,
        timeout: int = 600) -> tuple[int, str]:
    p = subprocess.run(
        cmd,
        cwd=str(cwd) if cwd else None,
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        errors="replace",
        timeout=timeout,
    )
    return p.returncode, p.stdout


def check_hygiene(base: str, ci: bool = False) -> list[str]:
    errors: list[str] = []

    # 1. Author identity check
    if ci:
        rc, log_out = run(["git", "log", f"{base}..HEAD", "--format=%an <%ae>"], cwd=ROOT)
        if rc == 0:
            for line in log_out.splitlines():
                if line.strip() and re.search(r"@(?:gmail|proton|icloud|outlook|hotmail|yahoo)\.", line, re.I):
                    errors.append(f"personal email in commit history: {line}")
    else:
        _, name = run(["git", "config", "user.name"], cwd=ROOT)
        _, email = run(["git", "config", "user.email"], cwd=ROOT)
        if name.strip() != "saltis" or not email.strip().endswith("@users.noreply.github.com"):
            errors.append(f"git identity must be saltis <*@users.noreply.github.com>, got: {name.strip()} <{email.strip()}>")

    # 2. Mesa AI commit trailer check on fork commits in base..HEAD
    rc, shas_out = run(["git", "rev-list", f"{base}..HEAD"], cwd=ROOT)
    if rc == 0:
        for sha in [s.strip() for s in shas_out.splitlines() if s.strip()]:
            _, msg = run(["git", "log", "-1", "--format=%B", sha], cwd=ROOT)
            if not AI_TRAILER_RE.search(msg):
                subj = msg.splitlines()[0] if msg.splitlines() else sha
                errors.append(f"commit {sha[:8]} ({subj}) missing 'Generated-by: LLM' or 'Assisted-by: LLM' trailer")

    # 3. Changed + untracked files: forbidden upstream Mesa dirs + privacy patterns
    rc, files_out = run(["git", "diff", "--name-only", base], cwd=ROOT)
    changed = [f for f in files_out.splitlines() if f.strip()] if rc == 0 else []
    _, untracked_out = run(["git", "ls-files", "--others", "--exclude-standard"], cwd=ROOT)
    changed += [f for f in untracked_out.splitlines() if f.strip()]

    for rel in sorted(set(changed)):
        for bad_dir in FORBIDDEN_DIRS:
            if rel.startswith(bad_dir):
                errors.append(f"forbidden Mesa AI-restricted directory modified: {rel}")
        if rel == "scripts/preflight_kk.py":
            continue
        p = ROOT / rel
        if not p.is_file() or p.stat().st_size > 2_000_000:
            continue
        try:
            text = p.read_text(encoding="utf-8", errors="ignore")
        except OSError:
            continue
        for pat in PRIVACY_PATTERNS:
            m = pat.search(text)
            if m:
                errors.append(f"privacy/local-path pattern {pat.pattern!r} matched in {rel}: {m.group(0)!r}")

    return errors


def build_targets(ws_dir: Path, archs: list[str]) -> list[Path]:
    env = os.environ.copy()
    env.pop("SDKROOT", None)
    env["PATH"] = (
        f"{ws_dir}/venv/bin:{ws_dir}/x86/bin:/opt/homebrew/opt/llvm/bin:"
        "/opt/homebrew/bin:/usr/bin:/bin:/usr/sbin:/sbin"
    )
    built: list[Path] = []
    target_rel = "src/kosmickrisp/vulkan/libvulkan_kosmickrisp.dylib"
    for arch in archs:
        bdir = ws_dir / ("build-x86" if arch == "x86_64" else "build")
        t0 = time.time()
        rc, out = run(["ninja", "-C", str(bdir), target_rel], env=env, timeout=600)
        if rc != 0:
            print(out[-4000:], file=sys.stderr)
            raise SystemExit(rc)
        dylib = bdir / target_rel
        print(f"build {arch} ok in {time.time() - t0:.1f}s: {dylib}", flush=True)
        built.append(dylib)
    return built


def install_crossover(ws_dir: Path, dst_dir: Path) -> None:
    src = ws_dir / "build-x86/src/kosmickrisp/vulkan/libvulkan_kosmickrisp.dylib"
    if not src.is_file():
        raise SystemExit(f"missing x86_64 dylib: {src}; pass --build first")
    dst = dst_dir / src.name
    shutil.copy2(src, dst)
    print(f"installed {dst}", flush=True)


def main() -> int:
    ap = argparse.ArgumentParser(description="KosmicKrisp fork hygiene, dual-arch build, and CrossOver install")
    ap.add_argument("--base", default="HEAD~1", help="Base ref for hygiene checks")
    ap.add_argument("--ws-dir", type=Path, default=ROOT.parent / "mesa-ws", help="Mesa build workspace directory")
    ap.add_argument("--crossover-lib", type=Path,
                    default=Path("/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/lib/x86_64"),
                    help="CrossOver x86_64 lib directory")
    ap.add_argument("--check-hygiene", action="store_true", help="Check Mesa AI policy, forbidden dirs, and privacy")
    ap.add_argument("--ci", action="store_true", help="CI mode for --check-hygiene")
    ap.add_argument("--build", action="store_true", help="Build KosmicKrisp ICD (x86_64 + arm64 by default)")
    ap.add_argument("--arch", action="append", choices=["x86_64", "arm64"], default=[], help="Target arch(es) to build")
    ap.add_argument("--install", action="store_true", help="Install x86_64 ICD into CrossOver Preview")
    args = ap.parse_args()

    if not (args.check_hygiene or args.build or args.install):
        args.check_hygiene = True

    if args.check_hygiene:
        errs = check_hygiene(args.base, ci=args.ci)
        if errs:
            print("HYGIENE FAILED:", file=sys.stderr)
            for e in errs:
                print(f"  - {e}", file=sys.stderr)
            return 1
        print("hygiene ok", flush=True)

    if args.build:
        archs = args.arch or ["x86_64", "arm64"]
        build_targets(args.ws_dir, archs)

    if args.install:
        install_crossover(args.ws_dir, args.crossover_lib)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
