#!/usr/bin/env python3
"""Assemble a deployable "<Name>.pak" folder under dist/.

Copies only the runtime payload (binaries, resources, curated data, pak.json,
launch.sh) -- source, docs, build scripts, and git metadata never make it in.
This mirrors what the release workflow ships in Music.Player.pak.zip, but
runs locally and produces a ready-to-copy folder instead of a zip.

Usage:
  python3 create_dist.py                      # both platforms, folder only
  python3 create_dist.py --platforms tg5040    # single platform
  python3 create_dist.py --zip                 # also write the release zip
  python3 create_dist.py --force               # ship despite missing binaries
"""

import argparse
import json
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).parent
PAK_JSON = ROOT / "pak.json"
DIST = ROOT / "dist"
ALL_PLATFORMS = ["tg5040", "tg5050"]

# Runtime payload only. Anything not listed here (src/, docs/, CLAUDE.md,
# build-*.sh, .git*) never ends up in the shipped pak.
INCLUDE = ["bin", "res", "state", "stations", "pak.json", "launch.sh"]

IGNORE = shutil.ignore_patterns(".DS_Store", ".gitkeep")


def load_pak():
    with open(PAK_JSON) as f:
        return json.load(f)


def missing_binaries(platforms):
    return [
        (platform, elf)
        for platform in platforms
        if not (elf := ROOT / "bin" / platform / "musicplayer.elf").exists()
    ]


def copy_payload(dest, platforms):
    dest.mkdir(parents=True)
    for name in INCLUDE:
        src = ROOT / name
        if not src.exists():
            print(f"WARNING: {name} not found, skipping", file=sys.stderr)
            continue
        target = dest / name
        if src.is_dir():
            shutil.copytree(src, target, ignore=IGNORE)
        else:
            shutil.copy2(src, target)

    # Only ship binaries for the requested platforms, drop the rest.
    bin_dir = dest / "bin"
    for platform_dir in bin_dir.iterdir():
        if platform_dir.is_dir() and platform_dir.name in ALL_PLATFORMS and platform_dir.name not in platforms:
            shutil.rmtree(platform_dir)


def dir_size(path):
    return sum(f.stat().st_size for f in path.rglob("*") if f.is_file())


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument(
        "--platforms",
        default=",".join(ALL_PLATFORMS),
        help=f"comma-separated platform list (default: {','.join(ALL_PLATFORMS)})",
    )
    parser.add_argument(
        "--zip", action="store_true", help="also write pak.json's release_filename zip"
    )
    parser.add_argument(
        "--force", action="store_true", help="ship even if a platform binary is missing"
    )
    args = parser.parse_args()

    platforms = [p.strip() for p in args.platforms.split(",") if p.strip()]
    unknown = [p for p in platforms if p not in ALL_PLATFORMS]
    if unknown:
        print(f"ERROR: unknown platform(s): {', '.join(unknown)}", file=sys.stderr)
        sys.exit(1)

    pak = load_pak()

    missing = missing_binaries(platforms)
    if missing and not args.force:
        for platform, elf in missing:
            print(f"ERROR: {elf} missing. Build it first: sh build-{platform}.sh", file=sys.stderr)
        print("(use --force to package anyway)", file=sys.stderr)
        sys.exit(1)

    pak_folder_name = f"{pak['name']}.pak"
    dest = DIST / pak_folder_name

    if DIST.exists():
        shutil.rmtree(DIST)

    copy_payload(dest, platforms)

    size_mb = dir_size(dest) / (1024 * 1024)
    print(f"dist/{pak_folder_name}  ({pak['version']}, {size_mb:.1f} MB)")
    for platform in platforms:
        elf = dest / "bin" / platform / "musicplayer.elf"
        print(f"  {platform}: {'ok' if elf.exists() else 'MISSING (shipped via --force)'}")

    if args.zip:
        zip_stem = DIST / pak["release_filename"].removesuffix(".zip")
        archive = shutil.make_archive(str(zip_stem), "zip", root_dir=dest)
        print(f"dist/{Path(archive).name}")


if __name__ == "__main__":
    main()
