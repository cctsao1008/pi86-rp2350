#!/usr/bin/env python3
"""Check first-party Markdown structure, links, and project documentation policy."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path
import re
import sys
from urllib.parse import unquote


EXCLUDED_PARTS = {".git", "build", "third_party", ".tools"}
HISTORICAL_POLICY_PARTS = {"releases", "retrospectives"}
EXCEPTIONS_PATH = Path("docs/document_policy_exceptions.txt")


@dataclass(frozen=True)
class Issue:
    path: Path
    line: int
    code: str
    message: str

    def format(self) -> str:
        location = f"{self.path.as_posix()}:{self.line}" if self.line else self.path.as_posix()
        return f"{location}: {self.code}: {self.message}"


def first_party_markdown(root: Path) -> list[Path]:
    files: list[Path] = []
    for path in root.rglob("*.md"):
        relative = path.relative_to(root)
        if any(part in EXCLUDED_PARTS for part in relative.parts):
            continue
        files.append(path)
    return sorted(files)


def parse_exceptions(root: Path) -> dict[tuple[str, str], str]:
    path = root / EXCEPTIONS_PATH
    if not path.exists():
        return {}

    exceptions: dict[tuple[str, str], str] = {}
    for number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split(maxsplit=2)
        if len(parts) != 3:
            raise ValueError(f"{EXCEPTIONS_PATH}:{number}: expected PATH CODE REASON")
        file_name, code, reason = parts
        exceptions[(file_name, code)] = reason
    return exceptions


def split_target(raw_target: str) -> str:
    target = raw_target.strip()
    if target.startswith("<") and ">" in target:
        return target[1 : target.index(">")]
    return target.split(maxsplit=1)[0]


def normalize_heading(value: str) -> str:
    value = re.sub(r"\[([^]]+)\]\([^)]+\)", r"\1", value)
    value = re.sub(r"[`*_~]", "", value)
    value = re.sub(r"\s+#+\s*$", "", value)
    return re.sub(r"\s+", " ", value.strip()).casefold()


def structural_issues(root: Path, path: Path) -> list[Issue]:
    relative = path.relative_to(root)
    text = path.read_text(encoding="utf-8")
    lines = text.splitlines()
    issues: list[Issue] = []
    fence_marker: str | None = None
    fence_line = 0
    heading_paths: dict[tuple[str, ...], int] = {}
    heading_stack: list[str] = []

    for number, line in enumerate(lines, 1):
        stripped = line.lstrip()
        opening = re.match(r"^(`{3,}|~{3,})", stripped)
        if opening:
            marker = opening.group(1)
            if fence_marker is None:
                fence_marker = marker[0]
                fence_line = number
            elif marker[0] == fence_marker:
                fence_marker = None
                fence_line = 0
            continue

        if fence_marker is not None:
            continue

        heading = re.match(r"^(#{1,6})\s+(.+?)\s*$", line)
        if heading:
            level = len(heading.group(1))
            normalized = normalize_heading(heading.group(2))
            heading_stack = heading_stack[: level - 1]
            heading_path = (*heading_stack, normalized)
            if heading_path in heading_paths:
                issues.append(
                    Issue(
                        relative,
                        number,
                        "MD003",
                        f"duplicate heading under the same parent; first used at line {heading_paths[heading_path]}",
                    )
                )
            else:
                heading_paths[heading_path] = number
            heading_stack.append(normalized)

        for match in re.finditer(r"!?\[[^]]*\]\(([^)]+)\)", line):
            target = unquote(split_target(match.group(1)))
            if not target or target.startswith(("#", "http://", "https://", "mailto:", "chatgpt-conversation:")):
                continue
            target = target.split("#", 1)[0].split("?", 1)[0]
            if not target:
                continue
            resolved = path.parent / target
            if not resolved.exists():
                issues.append(Issue(relative, number, "MD001", f"broken relative link: {target}"))

    if fence_marker is not None:
        issues.append(Issue(relative, fence_line, "MD002", "unclosed fenced code block"))

    return issues


def policy_issues(root: Path, path: Path) -> list[Issue]:
    relative = path.relative_to(root)
    text = path.read_text(encoding="utf-8")
    lower = text.casefold()
    issues: list[Issue] = []

    if relative.parts[:2] == ("docs", "validation"):
        checks = {
            "VAL001": (r"build target|firmware target|validation target|\buf2\b|\btarget\b", "missing firmware/test target identity"),
            "VAL002": (r"\bclock\b|\bmhz\b|\bkhz\b|stepped", "missing clock or timing identity"),
            "VAL003": (r"\bpass\b|\bfail\b|\binvalid\b|\baccepted\b", "missing explicit result"),
            "VAL004": (r"limitation|boundary|does not|scope|interpretation|conclusion", "missing scope or limitation statement"),
        }
        for code, (pattern, message) in checks.items():
            if not re.search(pattern, lower):
                issues.append(Issue(relative, 1, code, message))

        response_architecture = re.search(
            r"response architecture\s*:\s*\*{0,2}(?:"
            r"fixed\s*/\s*prestaged|"
            r"address-qualified|"
            r"bounded memory|"
            r"cached guaranteed hit|"
            r"general memory with defined miss policy|"
            r"integrated system)\b",
            lower,
        )
        if relative.name.casefold().startswith("ai_b") and response_architecture is None:
            issues.append(Issue(relative, 1, "VAL005", "missing or invalid response-architecture classification"))

    if "docs" in relative.parts and not any(part in HISTORICAL_POLICY_PARTS for part in relative.parts):
        for number, line in enumerate(text.splitlines(), 1):
            lowered = line.casefold()
            mentions_agent = re.search(r"\b(ai|codex|chatgpt|model)\b", lowered)
            mentions_cycle = re.search(r"\b(current[- ]cycle|active bus cycle|v30 bus cycle)\b", lowered)
            if not (mentions_agent and mentions_cycle):
                continue
            negated = re.search(r"\b(not|never|no|outside|must not|cannot|isn't|doesn't)\b", lowered)
            if not negated:
                issues.append(Issue(relative, number, "ARCH001", "AI/host agent appears in current-cycle path without an explicit exclusion"))

    return issues


def check_repository(root: Path) -> tuple[list[Issue], list[Issue]]:
    root = root.resolve()
    exceptions = parse_exceptions(root)
    failures: list[Issue] = []
    waived: list[Issue] = []

    for path in first_party_markdown(root):
        for issue in structural_issues(root, path) + policy_issues(root, path):
            key = (issue.path.as_posix(), issue.code)
            if key in exceptions and issue.code.startswith(("VAL", "ARCH")):
                waived.append(issue)
            else:
                failures.append(issue)

    return failures, waived


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--show-waived", action="store_true")
    args = parser.parse_args(argv)

    try:
        failures, waived = check_repository(args.root)
    except (OSError, UnicodeError, ValueError) as exc:
        print(f"DOC CHECK ERROR: {exc}", file=sys.stderr)
        return 2

    if args.show_waived:
        for issue in waived:
            print(f"WAIVED {issue.format()}")

    for issue in failures:
        print(issue.format())

    print(f"Markdown files checked = {len(first_party_markdown(args.root.resolve()))}")
    print(f"Policy waivers         = {len(waived)}")
    print(f"Documentation failures = {len(failures)}")
    print(f"DOCUMENTATION CHECK    = {'PASS' if not failures else 'FAIL'}")
    return 0 if not failures else 1


if __name__ == "__main__":
    raise SystemExit(main())
