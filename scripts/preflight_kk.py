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
PERSONAL_EMAIL_RE = re.compile(
    r"[A-Za-z0-9._%+-]+@(?:gmail|proton|protonmail|icloud|outlook|hotmail|yahoo|me|live|fastmail|hey)\.[A-Za-z]{2,}",
    re.I,
)
PRIVACY_PATTERNS = [
    ("personal-email", PERSONAL_EMAIL_RE),
    ("macos-home-path", re.compile(r"/Users/[A-Za-z0-9._-]+/")),
    ("windows-home-path", re.compile(r"C:[\\/]+Users[\\/]+[A-Za-z0-9._-]+[\\/]", re.I)),
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

    # 0. Base ref must resolve to a valid commit (never silently pass on bad refs)
    rc, base_sha_out = run(["git", "rev-parse", "--verify", "--quiet", f"{base}^{{commit}}"], cwd=ROOT)
    if rc != 0 or not base_sha_out.strip():
        return [f"invalid base ref: {base}"]
    base_sha = base_sha_out.strip()

    # 1. Local git config identity check (outside CI)
    if not ci:
        _, name = run(["git", "config", "user.name"], cwd=ROOT)
        _, email = run(["git", "config", "user.email"], cwd=ROOT)
        if name.strip() != "saltis" or not email.strip().endswith("@users.noreply.github.com"):
            errors.append(f"git identity must be saltis <*@users.noreply.github.com>, got: {name.strip()} <{email.strip()}>")

    # 2. Audit every commit introduced in base..HEAD (including merges for identity & privacy;
    #    Mesa AI trailer applies to non-merge commits).
    rc, log_out = run(
        ["git", "log", f"{base_sha}..HEAD", "--format=%H%x1f%P%x1f%an <%ae>%x1f%cn <%ce>%x1f%B%x1e"],
        cwd=ROOT,
    )
    commit_shas: list[str] = []
    if rc != 0:
        errors.append(f"git log failed for {base_sha}..HEAD: {log_out.strip()}")
    else:
        for rec in log_out.split("\x1e"):
            rec = rec.strip("\n")
            if not rec.strip():
                continue
            parts = rec.split("\x1f", 4)
            if len(parts) < 5:
                continue
            sha, parents, author, committer, msg = parts
            commit_shas.append(sha)
            is_merge = len(parents.split()) > 1
            for role, ident in (("author", author), ("committer", committer)):
                if PERSONAL_EMAIL_RE.search(ident):
                    errors.append(f"personal email in commit {sha[:8]} ({role}) [redacted]")
                else:
                    ok_fork = bool(re.match(r"^saltis <[^<>]+@users\.noreply\.github\.com>$", ident))
                    ok_gh_committer = role == "committer" and ident == "GitHub <noreply@github.com>"
                    if not (ok_fork or ok_gh_committer):
                        errors.append(f"commit {sha[:8]} ({role}) does not use fork identity 'saltis <*@users.noreply.github.com>'")
            for label, pat in PRIVACY_PATTERNS:
                if pat.search(msg):
                    errors.append(f"privacy pattern ({label}) matched in commit message {sha[:8]} [redacted]")
            if not is_merge and not AI_TRAILER_RE.search(msg):
                errors.append(f"commit {sha[:8]} missing 'Generated-by: LLM' or 'Assisted-by: LLM' trailer")

    # 3. Per-commit history audit: forbidden dirs (including deletions/renames) & added lines
    seen_forbidden: set[tuple[str, str]] = set()
    for sha in commit_shas:
        rc, ns_out = run(
            ["git", "diff-tree", "--no-commit-id", "--name-status", "-r", "-M", "-m", "--first-parent", sha],
            cwd=ROOT,
        )
        if rc != 0:
            errors.append(f"git diff-tree failed for {sha[:8]}: {ns_out.strip()}")
        else:
            for line in ns_out.splitlines():
                cols = line.split("\t")
                for rel in cols[1:]:
                    for bad_dir in FORBIDDEN_DIRS:
                        if rel.startswith(bad_dir) and (sha, rel) not in seen_forbidden:
                            seen_forbidden.add((sha, rel))
                            errors.append(f"forbidden Mesa AI-restricted directory touched in {sha[:8]}: {rel}")
        rc, patch_out = run(["git", "show", "--format=", "--unified=0", "-m", "--first-parent", sha], cwd=ROOT)
        if rc != 0:
            errors.append(f"git show failed for {sha[:8]}: {patch_out.strip()}")
        else:
            cur_file = "<unknown>"
            for line in patch_out.splitlines():
                if line.startswith("+++ b/"):
                    cur_file = line[6:]
                elif line.startswith("+") and not line.startswith("+++"):
                    added = line[1:]
                    for label, pat in PRIVACY_PATTERNS:
                        if pat.search(added):
                            errors.append(f"privacy pattern ({label}) added in commit {sha[:8]} ({cur_file}) [redacted]")
                            break

    # 4. Working tree (uncommitted + untracked) audit: forbidden dirs + privacy patterns
    wt_paths: list[str] = []
    rc, wt_ns = run(["git", "diff", "--name-status", "-M", "HEAD"], cwd=ROOT)
    if rc != 0:
        errors.append(f"git diff HEAD failed: {wt_ns.strip()}")
    else:
        for line in wt_ns.splitlines():
            cols = line.split("\t")
            wt_paths.extend(p for p in cols[1:] if p.strip())
    rc, untracked_out = run(["git", "ls-files", "--others", "--exclude-standard"], cwd=ROOT)
    if rc != 0:
        errors.append(f"git ls-files failed: {untracked_out.strip()}")
    else:
        wt_paths.extend(f for f in untracked_out.splitlines() if f.strip())

    for rel in sorted(set(wt_paths)):
        for bad_dir in FORBIDDEN_DIRS:
            if rel.startswith(bad_dir):
                errors.append(f"forbidden Mesa AI-restricted directory modified: {rel}")
        p = ROOT / rel
        if not p.is_file() or p.stat().st_size > 2_000_000:
            continue
        try:
            text = p.read_text(encoding="utf-8", errors="ignore")
        except OSError:
            continue
        for label, pat in PRIVACY_PATTERNS:
            if pat.search(text):
                errors.append(f"privacy pattern ({label}) matched in working tree file {rel} [redacted]")

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
