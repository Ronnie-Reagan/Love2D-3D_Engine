from __future__ import annotations

import argparse
import csv
import os
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path


@dataclass
class TimedCommandResult:
    command: list[str]
    cwd: Path
    started_utc: str
    duration_seconds: float
    returncode: int


def find_cmake() -> str:
    cmake = shutil.which("cmake")
    if cmake:
        return cmake

    windows_fallbacks = (
        Path(r"C:\Program Files\CMake\bin\cmake.exe"),
        Path(r"C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"),
    )
    for candidate in windows_fallbacks:
        if candidate.exists():
            return str(candidate)

    raise FileNotFoundError("cmake was not found on PATH and no known Windows install path exists")


def stringify_command(command: list[str]) -> str:
    return " ".join(f'"{part}"' if " " in part else part for part in command)


def run_command(command: list[str], cwd: Path) -> TimedCommandResult:
    started = datetime.now(timezone.utc).isoformat()
    started_perf = time.perf_counter()

    print("+", stringify_command(command))
    completed = subprocess.run(command, cwd=str(cwd), check=False)

    duration_seconds = time.perf_counter() - started_perf
    result = TimedCommandResult(
        command=command,
        cwd=cwd,
        started_utc=started,
        duration_seconds=duration_seconds,
        returncode=completed.returncode,
    )

    print(f"  -> exit={result.returncode} elapsed={result.duration_seconds:.3f}s")

    if completed.returncode != 0:
        raise subprocess.CalledProcessError(completed.returncode, command)

    return result


def find_ctest(cmake: str) -> str:
    sibling_name = "ctest.exe" if os.name == "nt" else "ctest"
    sibling = Path(cmake).with_name(sibling_name)
    if sibling.exists():
        return str(sibling)

    ctest = shutil.which("ctest")
    if ctest:
        return ctest

    raise FileNotFoundError("ctest was not found alongside cmake or on PATH")


def normalize_steamworks_sdk_root(candidate: Path) -> Path | None:
    candidate = candidate.resolve()
    if (candidate / "public" / "steam" / "steam_api.h").exists():
        return candidate
    if (candidate / "sdk" / "public" / "steam" / "steam_api.h").exists():
        return candidate / "sdk"
    return None


def detect_steamworks_sdk_root(source_dir: Path, explicit_root: str | None) -> Path | None:
    if explicit_root:
        resolved = normalize_steamworks_sdk_root(Path(explicit_root).expanduser())
        if resolved is None:
            raise FileNotFoundError(f"Steamworks SDK root was provided but could not be resolved: {explicit_root}")
        return resolved

    env_root = os.environ.get("STEAMWORKS_SDK_ROOT")
    if env_root:
        resolved = normalize_steamworks_sdk_root(Path(env_root).expanduser())
        if resolved is not None:
            return resolved

    workspace_candidates = (
        source_dir / "steamworks_sdk_164",
        source_dir / "steamworks_sdk_164" / "sdk",
    )
    for candidate in workspace_candidates:
        resolved = normalize_steamworks_sdk_root(candidate)
        if resolved is not None:
            return resolved

    return None


def detect_windows_icon_ico(source_dir: Path, explicit_icon: str | None) -> Path | None:
    if explicit_icon:
        candidate = Path(explicit_icon).expanduser().resolve()
        if not candidate.exists():
            raise FileNotFoundError(f"Windows icon .ico was provided but could not be found: {explicit_icon}")
        return candidate

    candidates = (
        source_dir / "portSource" / "Assets" / "Icons" / "TrueFlight.ico",
        source_dir / "portSource" / "Assets" / "Icons" / "AppIcon.ico",
        source_dir / "portSource" / "Assets" / "Icons" / "WindowIcon.ico",
    )
    for candidate in candidates:
        if candidate.exists():
            return candidate.resolve()
    return None


def normalize_cache_value(value: str) -> str:
    return os.path.normcase(value.replace("\\", "/")).rstrip("/")


def read_cmake_cache(cache_path: Path) -> dict[str, str]:
    cache_values: dict[str, str] = {}
    for line in cache_path.read_text(encoding="utf-8", errors="ignore").splitlines():
        if not line or line.startswith("//") or line.startswith("#"):
            continue
        key_and_type, separator, value = line.partition("=")
        if not separator:
            continue
        key, _, _cache_type = key_and_type.partition(":")
        if key:
            cache_values[key] = value
    return cache_values


def cmake_cache_matches_request(
    cache_path: Path,
    generator: str | None,
    arch: str | None,
    cmake_defines: list[str],
) -> bool:
    cache_values = read_cmake_cache(cache_path)

    if generator is not None:
        cached_generator = cache_values.get("CMAKE_GENERATOR")
        if cached_generator is None or normalize_cache_value(cached_generator) != normalize_cache_value(generator):
            return False

    if arch is not None:
        cached_arch = cache_values.get("CMAKE_GENERATOR_PLATFORM")
        if cached_arch is None or normalize_cache_value(cached_arch) != normalize_cache_value(arch):
            return False

    for define in cmake_defines:
        if not define.startswith("-D"):
            continue
        name, separator, value = define[2:].partition("=")
        if not separator:
            return False
        cached_value = cache_values.get(name)
        if cached_value is None or normalize_cache_value(cached_value) != normalize_cache_value(value):
            return False

    return True


def append_metrics_row(csv_path: Path, row: dict[str, str | int | float | bool]) -> None:
    csv_path.parent.mkdir(parents=True, exist_ok=True)

    fieldnames = [
        "timestamp_utc",
        "run_label",
        "phase",
        "status",
        "duration_seconds",
        "returncode",
        "command",
        "cwd",
        "source_dir",
        "build_dir",
        "config",
        "target",
        "generator",
        "arch",
        "clean_first",
        "reconfigure",
        "ctest_requested",
        "steamworks_enabled",
        "cache_state",
        "notes",
    ]

    file_exists = csv_path.exists()
    with csv_path.open("a", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        if not file_exists:
            writer.writeheader()
        writer.writerow(row)


def log_phase_metrics(
    metrics_csv: Path | None,
    run_label: str,
    phase: str,
    result: TimedCommandResult | None,
    *,
    status: str,
    source_dir: Path,
    build_dir: Path,
    config: str,
    target: str,
    generator: str | None,
    arch: str | None,
    clean_first: bool,
    reconfigure: bool,
    ctest_requested: bool,
    steamworks_enabled: bool,
    cache_state: str,
    notes: str = "",
    returncode_override: int | None = None,
) -> None:
    if metrics_csv is None:
        return

    append_metrics_row(
        metrics_csv,
        {
            "timestamp_utc": result.started_utc if result is not None else datetime.now(timezone.utc).isoformat(),
            "run_label": run_label,
            "phase": phase,
            "status": status,
            "duration_seconds": f"{(result.duration_seconds if result is not None else 0.0):.6f}",
            "returncode": returncode_override if returncode_override is not None else (result.returncode if result is not None else ""),
            "command": stringify_command(result.command) if result is not None else "",
            "cwd": str(result.cwd) if result is not None else "",
            "source_dir": str(source_dir),
            "build_dir": str(build_dir),
            "config": config,
            "target": target,
            "generator": generator or "",
            "arch": arch or "",
            "clean_first": int(clean_first),
            "reconfigure": int(reconfigure),
            "ctest_requested": int(ctest_requested),
            "steamworks_enabled": int(steamworks_enabled),
            "cache_state": cache_state,
            "notes": notes,
        },
    )


def configure_if_needed(
    cmake: str,
    source_dir: Path,
    build_dir: Path,
    generator: str | None,
    arch: str | None,
    reconfigure: bool,
    cmake_defines: list[str],
) -> tuple[TimedCommandResult | None, str]:
    cache_path = build_dir / "CMakeCache.txt"
    cache_state = "no_cache"

    if cache_path.exists():
        cache_state = "cache_miss"
        if not reconfigure and cmake_cache_matches_request(cache_path, generator, arch, cmake_defines):
            print("CMake configure step skipped: matching cache already present.")
            return None, "cache_hit"
        print("Cached CMake settings differ from the requested arguments; reconfiguring.")

    build_dir.mkdir(parents=True, exist_ok=True)
    command = [cmake, "-S", str(source_dir), "-B", str(build_dir)]
    command.extend(cmake_defines)
    if generator:
        command.extend(["-G", generator])
    if arch:
        command.extend(["-A", arch])

    return run_command(command, cwd=source_dir), cache_state


def build_target(
    cmake: str,
    build_dir: Path,
    config: str,
    target: str,
    clean_first: bool,
) -> TimedCommandResult:
    command = [cmake, "--build", str(build_dir), "--target", target]
    if config:
        command.extend(["--config", config])
    if clean_first:
        command.append("--clean-first")
    return run_command(command, cwd=build_dir)


def run_ctest(ctest: str, build_dir: Path, config: str) -> TimedCommandResult:
    command = [ctest, "--test-dir", str(build_dir), "--output-on-failure"]
    if config:
        command.extend(["-C", config])
    return run_command(command, cwd=build_dir)


def find_built_executable(build_dir: Path, config: str, target: str) -> Path | None:
    extensions = [".exe"] if os.name == "nt" else [""]
    candidate_dirs = [
        build_dir / "NativePort" / config,
        build_dir / config,
        build_dir / "NativePort",
        build_dir,
    ]

    for directory in candidate_dirs:
        for extension in extensions:
            candidate = directory / f"{target}{extension}"
            if candidate.exists():
                return candidate

    patterns = [f"{target}.exe"] if os.name == "nt" else [target]
    for pattern in patterns:
        matches = sorted(build_dir.rglob(pattern))
        if matches:
            return matches[0]

    return None


def sync_runtime_assets(runtime_dir: Path, source_dir: Path) -> None:
    assets_source = source_dir / "portSource"
    if not assets_source.exists():
        return

    assets_destination = runtime_dir / "portSource"
    shutil.copytree(assets_source, assets_destination, dirs_exist_ok=True)


def stage_runtime_package(runtime_dir: Path, package_dir: Path) -> Path:
    if runtime_dir.resolve() == package_dir.resolve():
        return runtime_dir

    if package_dir.exists():
        shutil.rmtree(package_dir)
    package_dir.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(runtime_dir, package_dir)
    return package_dir


def parse_args() -> argparse.Namespace:
    script_path = Path(__file__).resolve()
    workspace_root = script_path.parent.parent

    parser = argparse.ArgumentParser(description="Configure and build the native TrueFlight executable.")
    parser.add_argument(
        "--source-dir",
        default=str(workspace_root),
        help="CMake source directory. Defaults to the workspace root that wraps NativePort.",
    )
    parser.add_argument(
        "--build-dir",
        default=str(workspace_root / "build" / "workspace-layout"),
        help="CMake build directory. Defaults to build/workspace-layout.",
    )
    parser.add_argument(
        "--config",
        default="Release",
        help="Build configuration for multi-config generators. Defaults to Release.",
    )
    parser.add_argument(
        "--target",
        default="TrueFlight",
        help="CMake target to build. Defaults to TrueFlight.",
    )
    parser.add_argument(
        "--generator",
        default=None,
        help="Optional explicit CMake generator, for example 'Visual Studio 18 2026'.",
    )
    parser.add_argument(
        "--arch",
        default="x64" if os.name == "nt" else None,
        help="Optional generator architecture. Defaults to x64 on Windows.",
    )
    steamworks_group = parser.add_mutually_exclusive_group()
    steamworks_group.add_argument(
        "--enable-steamworks",
        action="store_true",
        help="Force Steamworks support on when a valid SDK root is available.",
    )
    steamworks_group.add_argument(
        "--disable-steamworks",
        action="store_true",
        help="Force Steamworks support off, even if a local SDK root exists.",
    )
    parser.add_argument(
        "--steamworks-sdk-root",
        default=None,
        help="Optional Steamworks SDK root or sdk/ directory. Defaults to auto-detecting steamworks_sdk_164 in the workspace or STEAMWORKS_SDK_ROOT.",
    )
    parser.add_argument(
        "--steam-app-id",
        default=480,
        type=int,
        help="Development Steam app id written to steam_appid.txt when Steamworks is enabled. Defaults to 480 (Spacewar).",
    )
    parser.add_argument(
        "--reconfigure",
        action="store_true",
        help="Force a CMake configure pass even if the build directory is already configured.",
        default=False,
    )
    parser.add_argument(
        "--clean-first",
        action="store_true",
        help="Pass --clean-first to the build step.",
    )
    parser.add_argument(
        "--ctest",
        action="store_true",
        help="Build the smoke-test target and run ctest --output-on-failure after the main build completes.",
    )
    parser.add_argument(
        "--metrics-csv",
        default=str(workspace_root / "build" / "compile_metrics.csv"),
        help="CSV file used to append timing metrics. Defaults to build/compile_metrics.csv.",
    )
    parser.add_argument(
        "--no-metrics",
        action="store_true",
        help="Disable CSV metrics logging entirely.",
    )
    parser.add_argument(
        "--run-label",
        default="",
        help="Optional label written to the CSV so runs can be grouped, for example 'ctest-cache-hit' or 'clean-build'.",
    )
    parser.add_argument(
        "--windows-icon-ico",
        default=None,
        help="Optional .ico file embedded as the Windows executable icon. Defaults to auto-detecting an .ico under portSource/Assets/Icons.",
    )
    parser.add_argument(
        "--package-dir",
        default="",
        help="Optional staged runtime package directory. Defaults to build/package/<config>/<target> under the source directory.",
    )
    parser.add_argument(
        "--no-package",
        action="store_true",
        help="Skip staging a packaged runtime directory after the build completes.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    source_dir = Path(args.source_dir).resolve()
    build_dir = Path(args.build_dir).resolve()
    metrics_csv = None if args.no_metrics else Path(args.metrics_csv).resolve()

    if not (source_dir / "CMakeLists.txt").exists():
        print(f"Source directory does not contain CMakeLists.txt: {source_dir}", file=sys.stderr)
        return 1

    try:
        cmake = find_cmake()
    except FileNotFoundError as error:
        print(str(error), file=sys.stderr)
        return 1

    steamworks_sdk_root = None
    steamworks_enabled = False
    if args.disable_steamworks:
        steamworks_enabled = False
    else:
        if args.enable_steamworks and os.name != "nt":
            print("Steamworks support is currently supported on Windows only.", file=sys.stderr)
            return 1
        try:
            steamworks_sdk_root = detect_steamworks_sdk_root(source_dir, args.steamworks_sdk_root)
        except FileNotFoundError as error:
            print(str(error), file=sys.stderr)
            return 1
        steamworks_enabled = bool(steamworks_sdk_root is not None and os.name == "nt")
        if args.enable_steamworks and steamworks_sdk_root is None:
            print(
                "Steamworks was explicitly enabled, but no SDK root was found. Pass --steamworks-sdk-root or set STEAMWORKS_SDK_ROOT.",
                file=sys.stderr,
            )
            return 1
        if args.enable_steamworks:
            steamworks_enabled = True

    cmake_defines = [
        f"-DTRUEFLIGHT_ENABLE_STEAMWORKS={'ON' if steamworks_enabled else 'OFF'}",
        f"-DTRUEFLIGHT_STEAM_APP_ID={args.steam_app_id}",
    ]
    if steamworks_sdk_root is not None:
        cmake_defines.append(f"-DSTEAMWORKS_SDK_ROOT={steamworks_sdk_root}")

    try:
        windows_icon_ico = detect_windows_icon_ico(source_dir, args.windows_icon_ico)
    except FileNotFoundError as error:
        print(str(error), file=sys.stderr)
        return 1
    if windows_icon_ico is not None:
        cmake_defines.append(f"-DTRUEFLIGHT_WINDOWS_ICON_ICO={windows_icon_ico}")

    run_label = args.run_label.strip()
    if not run_label:
        parts = [
            "ctest" if args.ctest else "build",
            "clean" if args.clean_first else "incremental",
            "reconfig" if args.reconfigure else "cached-config",
        ]
        run_label = "-".join(parts)

    print(f"Steamworks build mode: {'enabled' if steamworks_enabled else 'disabled'}")
    print(f"Windows exe icon: {windows_icon_ico if windows_icon_ico is not None else 'not set (.ico not found; PNG window icon still loads at runtime)'}")
    print(f"Metrics CSV: {metrics_csv if metrics_csv is not None else 'disabled'}")
    print(f"Run label: {run_label}")

    overall_started_utc = datetime.now(timezone.utc).isoformat()
    overall_started_perf = time.perf_counter()
    cache_state = "unknown"

    try:
        configure_result, cache_state = configure_if_needed(
            cmake=cmake,
            source_dir=source_dir,
            build_dir=build_dir,
            generator=args.generator,
            arch=args.arch,
            reconfigure=args.reconfigure,
            cmake_defines=cmake_defines,
        )

        if configure_result is None:
            log_phase_metrics(
                metrics_csv,
                run_label,
                "configure",
                None,
                status="skipped",
                source_dir=source_dir,
                build_dir=build_dir,
                config=args.config,
                target=args.target,
                generator=args.generator,
                arch=args.arch,
                clean_first=args.clean_first,
                reconfigure=args.reconfigure,
                ctest_requested=args.ctest,
                steamworks_enabled=steamworks_enabled,
                cache_state=cache_state,
                notes="matching CMake cache reused",
            )
        else:
            log_phase_metrics(
                metrics_csv,
                run_label,
                "configure",
                configure_result,
                status="ok",
                source_dir=source_dir,
                build_dir=build_dir,
                config=args.config,
                target=args.target,
                generator=args.generator,
                arch=args.arch,
                clean_first=args.clean_first,
                reconfigure=args.reconfigure,
                ctest_requested=args.ctest,
                steamworks_enabled=steamworks_enabled,
                cache_state=cache_state,
            )

        build_result = build_target(
            cmake=cmake,
            build_dir=build_dir,
            config=args.config,
            target=args.target,
            clean_first=args.clean_first,
        )
        log_phase_metrics(
            metrics_csv,
            run_label,
            "build",
            build_result,
            status="ok",
            source_dir=source_dir,
            build_dir=build_dir,
            config=args.config,
            target=args.target,
            generator=args.generator,
            arch=args.arch,
            clean_first=args.clean_first,
            reconfigure=args.reconfigure,
            ctest_requested=args.ctest,
            steamworks_enabled=steamworks_enabled,
            cache_state=cache_state,
        )

        if args.ctest:
            smoke_target = "TrueFlightNativeSmoke"
            if args.target != smoke_target:
                smoke_build_result = build_target(
                    cmake=cmake,
                    build_dir=build_dir,
                    config=args.config,
                    target=smoke_target,
                    clean_first=False,
                )
                log_phase_metrics(
                    metrics_csv,
                    run_label,
                    "build_smoke_target",
                    smoke_build_result,
                    status="ok",
                    source_dir=source_dir,
                    build_dir=build_dir,
                    config=args.config,
                    target=smoke_target,
                    generator=args.generator,
                    arch=args.arch,
                    clean_first=False,
                    reconfigure=args.reconfigure,
                    ctest_requested=args.ctest,
                    steamworks_enabled=steamworks_enabled,
                    cache_state=cache_state,
                    notes=f"main requested target was {args.target}",
                )

            ctest = find_ctest(cmake)
            test_result = run_ctest(
                ctest=ctest,
                build_dir=build_dir,
                config=args.config,
            )
            log_phase_metrics(
                metrics_csv,
                run_label,
                "ctest",
                test_result,
                status="ok",
                source_dir=source_dir,
                build_dir=build_dir,
                config=args.config,
                target=args.target,
                generator=args.generator,
                arch=args.arch,
                clean_first=args.clean_first,
                reconfigure=args.reconfigure,
                ctest_requested=args.ctest,
                steamworks_enabled=steamworks_enabled,
                cache_state=cache_state,
            )

    except subprocess.CalledProcessError as error:
        failed_duration = time.perf_counter() - overall_started_perf
        failed_command = TimedCommandResult(
            command=[str(part) for part in error.cmd] if isinstance(error.cmd, (list, tuple)) else [str(error.cmd)],
            cwd=build_dir,
            started_utc=overall_started_utc,
            duration_seconds=failed_duration,
            returncode=error.returncode,
        )
        log_phase_metrics(
            metrics_csv,
            run_label,
            "failed",
            failed_command,
            status="failed",
            source_dir=source_dir,
            build_dir=build_dir,
            config=args.config,
            target=args.target,
            generator=args.generator,
            arch=args.arch,
            clean_first=args.clean_first,
            reconfigure=args.reconfigure,
            ctest_requested=args.ctest,
            steamworks_enabled=steamworks_enabled,
            cache_state=cache_state,
            notes="command failed",
            returncode_override=error.returncode,
        )
        return error.returncode
    except FileNotFoundError as error:
        print(str(error), file=sys.stderr)
        return 1

    total_duration = time.perf_counter() - overall_started_perf
    total_result = TimedCommandResult(
        command=["<overall>"],
        cwd=build_dir,
        started_utc=overall_started_utc,
        duration_seconds=total_duration,
        returncode=0,
    )
    log_phase_metrics(
        metrics_csv,
        run_label,
        "overall",
        total_result,
        status="ok",
        source_dir=source_dir,
        build_dir=build_dir,
        config=args.config,
        target=args.target,
        generator=args.generator,
        arch=args.arch,
        clean_first=args.clean_first,
        reconfigure=args.reconfigure,
        ctest_requested=args.ctest,
        steamworks_enabled=steamworks_enabled,
        cache_state=cache_state,
    )

    executable = find_built_executable(build_dir, args.config, args.target)
    if executable is not None:
        print(f"Built executable: {executable}")
    else:
        print("Build finished, but the executable path could not be resolved automatically.", file=sys.stderr)
        return 2

    runtime_dir = executable.parent
    sync_runtime_assets(runtime_dir, source_dir)
    print(f"Runtime assets synced: {runtime_dir / 'portSource'}")

    if not args.no_package:
        package_dir = Path(args.package_dir).resolve() if args.package_dir else source_dir / "build" / "package" / args.config / args.target
        staged_dir = stage_runtime_package(runtime_dir, package_dir.resolve())
        print(f"Packaged runtime: {staged_dir}")
    print(f"Total Time Taken: {total_duration:.3f}s")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
