#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import re
import subprocess
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path

STABLE_TAG_RE = re.compile(r"^v(\d{4})\.(\d+)$")
RC_TAG_RE = re.compile(r"^v(\d{4})\.(\d+)-RC(\d+)$")
IGNORED_PREFIXES = ("chore(release):", "chore(nightly):", "ci(nightly):")


@dataclass(frozen=True, order=True)
class BaseVersion:
    year: int
    number: int

    def __str__(self) -> str:
        return f"{self.year}.{self.number}"


@dataclass(frozen=True, order=True)
class RcTag:
    base: BaseVersion
    rc: int
    tag: str


def run_git(args: list[str], cwd: Path) -> str:
    try:
        return subprocess.check_output(["git", *args], cwd=cwd, text=True).strip()
    except subprocess.CalledProcessError as exc:
        message = exc.output if exc.output else str(exc)
        raise RuntimeError(message) from exc


def parse_stable(text: str) -> BaseVersion | None:
    match = STABLE_TAG_RE.match(text.strip())
    if not match:
        return None
    return BaseVersion(int(match.group(1)), int(match.group(2)))


def parse_rc_tag(text: str) -> RcTag | None:
    match = RC_TAG_RE.match(text.strip())
    if not match:
        return None
    base = BaseVersion(int(match.group(1)), int(match.group(2)))
    rc = int(match.group(3))
    return RcTag(base=base, rc=rc, tag=text.strip())


def list_tags(repo: Path) -> tuple[list[tuple[str, BaseVersion]], list[RcTag]]:
    tags_raw = run_git(["tag", "--list", "v*"], cwd=repo)
    stable_tags: list[tuple[str, BaseVersion]] = []
    rc_tags: list[RcTag] = []

    for tag in tags_raw.splitlines():
        stable = parse_stable(tag)
        if stable:
            stable_tags.append((tag, stable))
            continue

        rc = parse_rc_tag(tag)
        if rc:
            rc_tags.append(rc)

    return stable_tags, rc_tags


def read_version_base(path: Path) -> BaseVersion | None:
    if not path.exists():
        return None
    raw = path.read_text(encoding="utf-8").strip()
    stable = parse_stable(raw)
    if stable:
        return stable

    rc = parse_rc_tag(raw)
    if rc:
        return rc.base

    return None


def commits_since(repo: Path, ref: str | None) -> list[tuple[str, str]]:
    range_spec = f"{ref}..HEAD" if ref else "HEAD"
    log = run_git(["log", range_spec, "--pretty=format:%s%n%b%n==END=="], cwd=repo)
    entries = [entry.strip() for entry in log.split("==END==") if entry.strip()]
    commits: list[tuple[str, str]] = []

    for entry in entries:
        lines = entry.splitlines()
        subject = lines[0] if lines else ""
        body = "\n".join(lines[1:]) if len(lines) > 1 else ""
        commits.append((subject.strip(), body))

    return commits


def is_meaningful_commit(subject: str) -> bool:
    lowered = subject.strip().lower()
    for prefix in IGNORED_PREFIXES:
        if lowered.startswith(prefix):
            return False
    return True


def next_base_version(
    version_from_file: BaseVersion | None, latest_stable: BaseVersion | None
) -> BaseVersion:
    if version_from_file and (not latest_stable or version_from_file > latest_stable):
        return version_from_file

    if latest_stable:
        current_year = datetime.now(timezone.utc).year
        if latest_stable.year < current_year:
            return BaseVersion(current_year, 1)
        return BaseVersion(latest_stable.year, latest_stable.number + 1)

    if version_from_file:
        return version_from_file

    return BaseVersion(datetime.now(timezone.utc).year, 1)


def latest_rc_for_base(rc_tags: list[RcTag], base: BaseVersion) -> RcTag | None:
    candidates = [tag for tag in rc_tags if tag.base == base]
    if not candidates:
        return None
    return max(candidates, key=lambda tag: tag.rc)


def compute_nightly(repo: Path) -> dict[str, str]:
    stable_tags, rc_tags = list_tags(repo)
    latest_stable_entry = max(stable_tags, key=lambda item: item[1]) if stable_tags else None
    latest_stable_tag = latest_stable_entry[0] if latest_stable_entry else ""
    latest_stable = latest_stable_entry[1] if latest_stable_entry else None

    version_from_file = read_version_base(repo / "version.txt")
    base = next_base_version(version_from_file, latest_stable)

    latest_for_base = latest_rc_for_base(rc_tags, base)
    next_rc = (latest_for_base.rc + 1) if latest_for_base else 1
    version = f"{base}-RC{next_rc}"
    tag = f"v{version}"

    latest_nightly = max(rc_tags, key=lambda item: (item.base, item.rc)) if rc_tags else None
    latest_nightly_tag = latest_nightly.tag if latest_nightly else ""
    commits_after_nightly = commits_since(repo, latest_nightly_tag or None)
    meaningful_after_nightly = [
        commit for commit in commits_after_nightly if is_meaningful_commit(commit[0])
    ]
    commits_since_nightly = len(meaningful_after_nightly)
    head_sha = run_git(["rev-parse", "HEAD"], cwd=repo)

    return {
        "version": version,
        "tag": tag,
        "base_version": str(base),
        "latest_stable_tag": latest_stable_tag,
        "latest_nightly_tag": latest_nightly_tag,
        "commits_since_nightly": str(commits_since_nightly),
        "head_sha": head_sha,
    }


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Compute BrumSchtick nightly tag and release decision."
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="Force release even when no commits exist since the previous nightly.",
    )
    parser.add_argument(
        "--write",
        metavar="PATH",
        help="Optional file path to write the computed nightly tag.",
    )
    parser.add_argument(
        "--format",
        choices=["text", "json", "github"],
        default="text",
        help="Output format.",
    )
    args = parser.parse_args()

    repo = Path(__file__).resolve().parent.parent
    payload = compute_nightly(repo)
    should_release = args.force or int(payload["commits_since_nightly"]) > 0
    payload["should_release"] = "true" if should_release else "false"

    if args.write:
        Path(args.write).write_text(payload["tag"] + "\n", encoding="utf-8")

    if args.format == "json":
        print(json.dumps(payload, indent=2))
        return 0
    if args.format == "github":
        for key, value in payload.items():
            print(f"{key}={value}")
        return 0

    print(payload["tag"])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
