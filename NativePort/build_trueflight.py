#!/usr/bin/env python3
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path


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


def run_command(command: list[str], cwd: Path) -> None:
    print("+", " ".join(f'"{part}"' if " " in part else part for part in command))
    subprocess.run(command, cwd=str(cwd), check=True)


def configure_if_needed(
    cmake: str,
    source_dir: Path,
    build_dir: Path,
    generator: str | None,
    arch: str | None,
    reconfigure: bool,
) -> None:
    cache_path = build_dir / "CMakeCache.txt"
    if cache_path.exists() and not reconfigure:
        return

    build_dir.mkdir(parents=True, exist_ok=True)
    command = [cmake, "-S", str(source_dir), "-B", str(build_dir)]
    if generator:
        command.extend(["-G", generator])
    if arch:
        command.extend(["-A", arch])
    run_command(command, cwd=source_dir)


def build_target(
    cmake: str,
    build_dir: Path,
    config: str,
    target: str,
    clean_first: bool,
) -> None:
    command = [cmake, "--build", str(build_dir), "--target", target]
    if config:
        command.extend(["--config", config])
    if clean_first:
        command.append("--clean-first")
    run_command(command, cwd=build_dir)


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
    parser.add_argument(
        "--reconfigure",
        action="store_true",
        help="Force a CMake configure pass even if the build directory is already configured.",
        default=True
    )
    parser.add_argument(
        "--clean-first",
        action="store_true",
        help="Pass --clean-first to the build step.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    source_dir = Path(args.source_dir).resolve()
    build_dir = Path(args.build_dir).resolve()

    if not (source_dir / "CMakeLists.txt").exists():
        print(f"Source directory does not contain CMakeLists.txt: {source_dir}", file=sys.stderr)
        return 1

    try:
        cmake = find_cmake()
    except FileNotFoundError as error:
        print(str(error), file=sys.stderr)
        return 1

    try:
        configure_if_needed(
            cmake=cmake,
            source_dir=source_dir,
            build_dir=build_dir,
            generator=args.generator,
            arch=args.arch,
            reconfigure=args.reconfigure,
        )
        build_target(
            cmake=cmake,
            build_dir=build_dir,
            config=args.config,
            target=args.target,
            clean_first=args.clean_first,
        )
    except subprocess.CalledProcessError as error:
        return error.returncode

    executable = find_built_executable(build_dir, args.config, args.target)
    if executable is not None:
        print(f"Built executable: {executable}")
    else:
        print("Build finished, but the executable path could not be resolved automatically.", file=sys.stderr)
        return 2

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
