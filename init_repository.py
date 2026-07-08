#!/usr/bin/env python3
"""Sourcetrail repository initialization.

Run this once after cloning (and again after pulling when submodules moved):

    python init_repository.py

Windows link policy: junctions, not symlinks.
------------------------------------------------
Symbolic links are a recurring problem on Windows: creating them requires
Developer Mode or an elevated process, `git clone` needs an explicit
`core.symlinks=true`, and CMake's `file(COPY)` mangles them. This repository
therefore does not depend on symlinks on Windows at all:

  - The in-repo test-data symlinks (bin/test/data/...) stay as the plain
    placeholder files a default Windows clone produces. The tests that verify
    symlink semantics skip on Windows anyway (see ASSERT_SYMLINK_PLATFORM in
    src/test/Catch2.hpp), and every other test works with the placeholders.
  - The per-build-directory test-data link (<build>/test/data) is a directory
    junction, which behaves like a directory symlink for our purposes but
    needs no privileges. CMake creates it at configure time; the
    `link-test-data` command below does the same for manual setups.

On Linux/macOS symlinks work fine and are used as before.
"""

import argparse
import logging
import os
import stat
import subprocess
import sys
from pathlib import Path
from typing import List, Union

REPO_ROOT = Path(__file__).resolve().parent

TEST_DATA_SOURCE = REPO_ROOT / "bin" / "test" / "data"


def init_logger() -> logging.Logger:
    logger = logging.getLogger()
    logger.setLevel(logging.INFO)
    handler = logging.StreamHandler()
    handler.setFormatter(logging.Formatter("[%(levelname)s]\t: %(message)s"))
    logger.addHandler(handler)
    return logger


def is_junction(path: Union[str, Path]) -> bool:
    """Check if the given path is a junction (directory reparse point) on Windows."""
    if sys.platform != "win32":
        return False
    try:
        st = os.stat(path, follow_symlinks=False)
    except OSError:
        return False
    if not st.st_file_attributes & stat.FILE_ATTRIBUTE_REPARSE_POINT:
        return False
    return getattr(st, "st_reparse_tag", 0) == stat.IO_REPARSE_TAG_MOUNT_POINT


def is_link(path: Union[str, Path]) -> bool:
    """Check if the given path is a link (junction on Windows, symlink otherwise)."""
    return is_junction(path) if sys.platform == "win32" else os.path.islink(path)


def remove_link(link: Union[str, Path], logger: logging.Logger) -> bool:
    """Remove a link (junction on Windows, symlink otherwise). Never recurses
    into the link target."""
    if not os.path.lexists(link) and not is_junction(link):
        return True

    try:
        if is_junction(link):
            os.rmdir(link)  # removes the junction, not its target's contents
        elif os.path.islink(link):
            os.unlink(link)
        else:
            logging.error(f"{link} exists but is not a link, refusing to remove it")
            return False
        logger.info(f"Removed link: {link}")
        return True
    except OSError as e:
        logger.error(f"Failed to remove link {link}: {e}")
        return False


def create_link(link: Union[str, Path], target: Union[str, Path], logger: logging.Logger) -> bool:
    """Create a directory link (junction on Windows, symlink otherwise) from
    link to target. An existing link is replaced; an existing real directory is
    left alone and reported as an error."""
    if is_link(link):
        if not remove_link(link, logger):
            return False
    elif os.path.exists(link):
        logger.error(f"{link} already exists and is not a link, refusing to replace it")
        return False

    Path(link).parent.mkdir(parents=True, exist_ok=True)

    if sys.platform == "win32":
        # Junctions need no privileges (symlinks require Developer Mode or an
        # elevated process). mklink is a cmd builtin, not an executable.
        logger.info(f"Creating junction [{link}] -> [{target}]")
        try:
            subprocess.run(
                ["cmd", "/c", "mklink", "/J", str(link), str(target)],
                check=True,
                capture_output=True,
                text=True,
                encoding="utf-8",
                errors="replace",
            )
            return True
        except subprocess.CalledProcessError as e:
            logger.error(f"Failed to create junction: {e.stderr.strip()}")
            return False
    else:
        logger.info(f"Creating symbolic link [{link}] -> [{target}]")
        try:
            os.symlink(target, link, target_is_directory=True)
            return True
        except OSError as e:
            logger.error(f"Failed to create symbolic link: {e}")
            return False


def run_git_command(command: List[str], logger: logging.Logger) -> bool:
    git_command = ["git", "-C", str(REPO_ROOT)] + command
    logger.info(f"Executing:\t{' '.join(git_command)}")
    try:
        subprocess.run(
            git_command,
            check=True,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
        )
        return True
    except subprocess.CalledProcessError as e:
        logger.error(f"Git command failed: {e.stderr.strip()}")
        return False


def check_symlink_configuration(logger: logging.Logger) -> bool:
    """Windows only: report how the working tree was checked out.

    Neither state is an error -- the build and the tests work with both -- but
    the state is worth knowing: with core.symlinks=false (the Windows default)
    the in-repo test-data symlinks are placeholder files and the symlink
    specific tests skip (they skip on Windows either way, see
    ASSERT_SYMLINK_PLATFORM)."""
    if sys.platform != "win32":
        return True

    result = subprocess.run(
        ["git", "-C", str(REPO_ROOT), "config", "--get", "core.symlinks"],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    value = result.stdout.strip() or "<unset>"
    logger.info(
        f"core.symlinks is '{value}': symlinks are not required on Windows, "
        "junctions are used instead."
    )
    return True


def bootstrap_vcpkg(logger: logging.Logger) -> bool:
    """Bootstrap the vcpkg submodule if its executable is missing."""
    vcpkg_dir = REPO_ROOT / "vcpkg"
    vcpkg_exe = vcpkg_dir / ("vcpkg.exe" if sys.platform == "win32" else "vcpkg")
    if vcpkg_exe.exists():
        logger.info(f"vcpkg already bootstrapped: {vcpkg_exe}")
        return True

    script = vcpkg_dir / ("bootstrap-vcpkg.bat" if sys.platform == "win32" else "bootstrap-vcpkg.sh")
    if not script.exists():
        logger.error(f"vcpkg bootstrap script not found: {script} (submodules initialized?)")
        return False

    logger.info(f"Bootstrapping vcpkg: {script}")
    try:
        subprocess.run([str(script), "-disableMetrics"], check=True, cwd=str(vcpkg_dir))
        return True
    except (subprocess.CalledProcessError, OSError) as e:
        logger.error(f"Failed to bootstrap vcpkg: {e}")
        return False


def link_test_data(build_dir: Path, logger: logging.Logger) -> bool:
    """Link <build_dir>/test/data to bin/test/data (junction on Windows).

    CMake does the same at configure time; this command exists for manually
    prepared build directories and for repairing a broken link."""
    if not TEST_DATA_SOURCE.is_dir():
        logger.error(f"Test data directory not found: {TEST_DATA_SOURCE}")
        return False

    link = build_dir / "test" / "data"
    return create_link(link, TEST_DATA_SOURCE, logger)


def init_repository(logger: logging.Logger) -> bool:
    logger.info("Initializing Sourcetrail repository structure")
    return all(
        [
            run_git_command(["submodule", "update", "--init", "--recursive"], logger),
            check_symlink_configuration(logger),
            bootstrap_vcpkg(logger),
        ]
    )


def main() -> int:
    logger = init_logger()
    parser = argparse.ArgumentParser(description="Sourcetrail repository bootstrap.")
    sub = parser.add_subparsers(dest="command")

    init_cmd = sub.add_parser(
        "init",
        help="Repository structure: submodules, vcpkg bootstrap. "
             "(Default when no subcommand is given.)")
    init_cmd.set_defaults(command="init")

    link_cmd = sub.add_parser(
        "link-test-data",
        help="Create the <build-dir>/test/data link to bin/test/data "
             "(junction on Windows, symlink otherwise).")
    link_cmd.add_argument(
        "--build-dir", required=True, type=Path,
        help="CMake build directory (e.g. .build/rel).")

    args = parser.parse_args()
    command = args.command or "init"

    if command == "init":
        return 0 if init_repository(logger) else 1
    if command == "link-test-data":
        return 0 if link_test_data(args.build_dir.resolve(), logger) else 1

    logger.error(f"Unknown command: {command}")
    return 1


if __name__ == "__main__":
    sys.exit(main())
