#!/usr/bin/env python3
"""Run TC39 Test262 against the built TurboJS CLI.

The runner is intentionally resilient: child output is captured as bytes and
losslessly escaped on decode errors, every worker exception becomes an isolated
harness-error record, and checkpoint reports allow interrupted runs to resume.
"""
from __future__ import annotations

import argparse
import concurrent.futures
import json
import os
import re
import subprocess
import tempfile
import threading
import time
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any

import yaml

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SUITE = ROOT / "third_party" / "test262"
FRONT = re.compile(r"/\*---\s*(.*?)\s*---\*/", re.S)
UNSUPPORTED = {
    "$262.createRealm": "host-createRealm",
    "$262.detachArrayBuffer": "host-detachArrayBuffer",
    "$262.agent.": "host-agent",
    "$262.IsHTMLDDA": "host-IsHTMLDDA",
}
HOST_INCLUDE_SKIPS = {
    "detachArrayBuffer.js": "host-detachArrayBuffer",
    "nans.js": None,
}
CORE_SKIP_FEATURES = {"Temporal"}


def metadata(path: Path) -> tuple[str, dict[str, Any]]:
    text = path.read_text(encoding="utf-8", errors="replace")
    match = FRONT.search(text)
    data = yaml.safe_load(match.group(1)) if match else {}
    return text, data or {}


def variants(meta: dict[str, Any]) -> list[tuple[str, bool, bool]]:
    flags = set(meta.get("flags", []) or [])
    if "module" in flags:
        return [("module", True, False)]
    if "raw" in flags:
        return [("raw", False, False)]
    if "onlyStrict" in flags:
        return [("strict", False, True)]
    if "noStrict" in flags:
        return [("sloppy", False, False)]
    return [("sloppy", False, False), ("strict", False, True)]


def decode_output(data: bytes | None) -> str:
    """Decode arbitrary child output without allowing locale failures.

    Test262 intentionally exercises strings and byte sequences that are not
    representable in the Windows console code page. UTF-8 with backslash
    replacement keeps the report valid and preserves offending bytes visibly.
    """
    if not data:
        return ""
    return data.decode("utf-8", errors="backslashreplace")


def result_key(result: dict[str, Any]) -> str:
    return f"{result['test']}\0{result['variant']}"


def run_one(job: tuple[Path, Path, Path, Path, tuple[str, bool, bool], float]) -> dict[str, Any]:
    cli, suite, host, path, variant, timeout = job
    rel = path.relative_to(suite / "test").as_posix()
    started = time.perf_counter()
    temp: Path | None = None
    try:
        source, meta = metadata(path)
        flags = set(meta.get("flags", []) or [])
        for token, reason in UNSUPPORTED.items():
            if token in source:
                return {"test": rel, "variant": variant[0], "status": "skip", "detail": reason, "seconds": 0.0}
        if "CanBlockIsFalse" in flags:
            return {"test": rel, "variant": variant[0], "status": "skip", "detail": "unsupported-CanBlockIsFalse", "seconds": 0.0}

        meta_includes = list(meta.get("includes", []) or [])
        for include_name in meta_includes:
            reason = HOST_INCLUDE_SKIPS.get(include_name)
            if reason:
                return {"test": rel, "variant": variant[0], "status": "skip", "detail": reason, "seconds": 0.0}
        includes = [] if "raw" in flags else ["sta.js", "assert.js"]
        includes += meta_includes
        cmd = [str(cli)]
        for include in [host, *[suite / "harness" / name for name in includes]]:
            cmd += ["-I", str(include)]
        if variant[1]:
            cmd.append("-m")

        test_path = path
        if variant[2]:
            fd, name = tempfile.mkstemp(prefix="turbojs-test262-", suffix=".js")
            os.close(fd)
            temp = Path(name)
            temp.write_text('"use strict";\n' + source, encoding="utf-8")
            test_path = temp
        cmd.append(str(test_path))

        try:
            creationflags = 0
            if os.name == "nt":
                creationflags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
            completed = subprocess.run(
                cmd, cwd=path.parent, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                timeout=timeout, text=False, check=False,
                creationflags=creationflags,
            )
        except subprocess.TimeoutExpired as exc:
            output = "\n".join(filter(None, [decode_output(exc.stdout), decode_output(exc.stderr)])).strip()
            return {"test": rel, "variant": variant[0], "status": "timeout",
                    "detail": output[-1000:] or str(exc), "seconds": time.perf_counter() - started}

        output = "\n".join(
            filter(None, [decode_output(completed.stdout), decode_output(completed.stderr)])
        ).strip()
        negative = meta.get("negative") or {}
        expected_type = str(negative.get("type", ""))
        expected_phase = str(negative.get("phase", ""))
        if negative:
            ok = completed.returncode != 0
            if expected_type and expected_type not in output:
                ok = False
            status = "pass" if ok else "fail"
            detail = "" if ok else (
                f"expected {expected_phase}:{expected_type}, exit={completed.returncode}; "
                f"{output[-1000:]}"
            )
        else:
            status = "pass" if completed.returncode == 0 else "fail"
            detail = "" if status == "pass" else output[-1000:]
        return {
            "test": rel,
            "variant": variant[0],
            "status": status,
            "detail": detail,
            "seconds": time.perf_counter() - started,
        }
    except BaseException as exc:  # isolate malformed tests and platform subprocess anomalies
        return {
            "test": rel,
            "variant": variant[0],
            "status": "harness-error",
            "detail": f"{type(exc).__name__}: {exc}",
            "seconds": time.perf_counter() - started,
        }
    finally:
        if temp is not None:
            temp.unlink(missing_ok=True)


def make_report(
    cli: Path,
    suite: Path,
    started: float,
    counts: Counter[str],
    results: list[dict[str, Any]],
    completed: bool,
) -> dict[str, Any]:
    categories: defaultdict[str, Counter[str]] = defaultdict(Counter)
    for result in results:
        categories[result["test"].split("/", 1)[0]][result["status"]] += 1
    return {
        "engine": str(cli),
        "suite": str(suite),
        "started": started,
        "elapsed": time.time() - started,
        "completed": completed,
        "counts": dict(counts),
        "categories": {name: dict(values) for name, values in sorted(categories.items())},
        "results": results,
    }


def write_report(path: Path, report: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(report, indent=2, ensure_ascii=True), encoding="utf-8")
    os.replace(temporary, path)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--suite", type=Path, default=DEFAULT_SUITE)
    parser.add_argument(
        "--engine",
        type=Path,
        default=ROOT / "build" / "full-release" / ("turbojs.exe" if os.name == "nt" else "turbojs"),
    )
    parser.add_argument("--workers", type=int, default=max(1, min(16, os.cpu_count() or 1)))
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--filter", default="")
    parser.add_argument("--limit", type=int, default=0)
    parser.add_argument("--single-variant", action="store_true", help="Run one standards-relevant variant per file")
    parser.add_argument("--shard-index", type=int, default=0)
    parser.add_argument("--shard-count", type=int, default=1)
    parser.add_argument("--report", type=Path, default=ROOT / "build" / "test262-report.json")
    parser.add_argument("--resume", action="store_true", help="Resume completed test/variant pairs from --report")
    parser.add_argument("--checkpoint-every", type=int, default=250, help="Persist an atomic partial report every N results")
    parser.add_argument("--allow-failures", action="store_true", help="Return success after a completed conformance run")
    parser.add_argument("--profile", choices=("full", "core"), default="full",
                        help="full runs all tests; core excludes Intl402 and optional Temporal")
    parser.add_argument("--retry-timeouts", type=int, default=1,
                        help="retry timed-out executions this many times with doubled timeout")
    args = parser.parse_args()

    suite = args.suite.resolve()
    cli = args.engine.resolve()
    host = (ROOT / "tests/test262/host.js").resolve()
    if not cli.is_file():
        parser.error(f"engine not found: {cli}")
    if args.workers < 1:
        parser.error("workers must be positive")
    if args.timeout <= 0:
        parser.error("timeout must be positive")
    if args.checkpoint_every < 1:
        parser.error("checkpoint-every must be positive")

    paths = sorted((suite / "test").rglob("*.js"))
    if args.filter:
        paths = [path for path in paths if args.filter in path.relative_to(suite / "test").as_posix()]
    if args.shard_count < 1 or not 0 <= args.shard_index < args.shard_count:
        parser.error("shard index must be in [0, shard-count)")
    paths = [path for index, path in enumerate(paths) if index % args.shard_count == args.shard_index]
    if args.limit:
        paths = paths[: args.limit]

    jobs = []
    profile_skips: list[dict[str, Any]] = []
    for path in paths:
        _, meta = metadata(path)
        selected_variants = variants(meta)
        if args.single_variant and len(selected_variants) > 1:
            selected_variants = selected_variants[:1]
        rel = path.relative_to(suite / "test").as_posix()
        features = set(meta.get("features", []) or [])
        profile_reason = None
        if args.profile == "core":
            if rel.startswith("intl402/"):
                profile_reason = "profile-core-intl402"
            elif features & CORE_SKIP_FEATURES:
                profile_reason = "profile-core-Temporal"
        if profile_reason:
            profile_skips.extend({"test": rel, "variant": v[0], "status": "skip", "detail": profile_reason, "seconds": 0.0} for v in selected_variants)
        else:
            jobs += [(cli, suite, host, path, variant, args.timeout) for variant in selected_variants]

    results: list[dict[str, Any]] = list(profile_skips)
    completed_keys: set[str] = set()
    started = time.time()
    if args.resume and args.report.is_file():
        try:
            previous = json.loads(args.report.read_text(encoding="utf-8"))
            results = list(previous.get("results", []))
            completed_keys = {result_key(result) for result in results}
            started = float(previous.get("started", started))
        except (OSError, ValueError, TypeError) as exc:
            parser.error(f"cannot resume report {args.report}: {exc}")

    pending_jobs = []
    for job in jobs:
        path, variant = job[3], job[4]
        key = f"{path.relative_to(suite / 'test').as_posix()}\0{variant[0]}"
        if key not in completed_keys:
            pending_jobs.append(job)

    counts = Counter(result["status"] for result in results)
    total_expected = len(profile_skips) + len(jobs)
    print(
        f"TurboJS Test262: files={len(paths)} executions={total_expected} "
        f"pending={len(pending_jobs)} workers={args.workers}",
        flush=True,
    )
    if completed_keys:
        print(f"Resuming {len(completed_keys)} completed executions from {args.report}", flush=True)

    report_lock = threading.Lock()
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as executor:
        for pending_index, result in enumerate(executor.map(run_one, pending_jobs), 1):
            results.append(result)
            counts[result["status"]] += 1
            total_done = len(results)
            if pending_index % args.checkpoint_every == 0:
                with report_lock:
                    write_report(args.report, make_report(cli, suite, started, counts, results, completed=False))
            if total_done % 1000 == 0 or pending_index == len(pending_jobs):
                print(
                    f"[{total_done}/{total_expected}] pass={counts['pass']} fail={counts['fail']} "
                    f"skip={counts['skip']} timeout={counts['timeout']} "
                    f"harness-error={counts['harness-error']}",
                    flush=True,
                )

    report = make_report(cli, suite, started, counts, results, completed=True)
    write_report(args.report, report)
    print(json.dumps({"counts": dict(counts), "elapsed": report["elapsed"], "report": str(args.report)}, indent=2))
    if args.allow_failures:
        return 0
    return 1 if counts["fail"] or counts["timeout"] or counts["harness-error"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
