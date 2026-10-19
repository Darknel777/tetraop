#!/usr/bin/env python3
"""Check a built Xen Milenium VST3 bundle. Exits 0 only when every check passes."""

import json
import pathlib
import re
import subprocess
import sys

FORBIDDEN = (
    "Xen0",
    "Xn20",
    "com.XEN.XEN2000",
    "XEN 2000",
    "XEN2000",
)


def fail(message: str) -> None:
    print(f"FAIL: {message}")
    sys.exit(1)


def main() -> None:
    if len(sys.argv) != 2:
        fail("usage: check_vst3_identity.py <build-directory>")

    build_dir = pathlib.Path(sys.argv[1])
    if not build_dir.is_dir():
        fail(f"build directory does not exist: {build_dir}")

    infos = sorted(build_dir.rglob("moduleinfo.json"))
    if len(infos) != 1:
        fail(f"expected one moduleinfo.json, found {len(infos)}")

    info_path = infos[0]
    bundle = info_path.parent.parent.parent
    if bundle.suffix != ".vst3":
        fail(f"moduleinfo.json is not inside a .vst3 bundle: {info_path}")

    # JUCE writes moduleinfo.json with trailing commas. Strip them, then parse.
    info_text = re.sub(r",(\s*[}\]])", r"\1", info_path.read_text(encoding="utf-8"))
    data = json.loads(info_text)
    if data.get("Name") != "Xen Milenium":
        fail(f"bundle Name is {data.get('Name')!r}")

    classes = data.get("Classes") or []
    if not classes:
        fail("moduleinfo.json has no Classes")

    for entry in classes:
        if entry.get("Name") != "Xen Milenium":
            fail(f"class Name is {entry.get('Name')!r}")
        vendor = str(entry.get("Vendor", ""))
        if vendor != "XenMilenium":
            fail(f"class Vendor is {vendor!r}")
        categories = entry.get("Sub Categories") or []
        has_instrument = any(item == "Instrument" or item.startswith("Instrument") for item in categories)
        has_synth = any(item == "Synth" or item.endswith("Synth") for item in categories)
        if not has_instrument or not has_synth:
            fail(f"Sub Categories are {categories!r}")

    binaries = [path for path in bundle.rglob("*") if path.is_file() and path.suffix == ".so"]
    if not binaries:
        fail(f"no shared library inside {bundle}")

    for binary in binaries:
        if binary.stat().st_size <= 0:
            fail(f"empty binary: {binary}")
        file_info = subprocess.run(
            ["file", "-b", str(binary)],
            check=True,
            capture_output=True,
            text=True,
        )
        if "ELF" not in file_info.stdout:
            fail(f"binary is not ELF: {binary}: {file_info.stdout.strip()}")
        strings = subprocess.run(
            ["strings", "-a", str(binary)],
            check=True,
            capture_output=True,
            text=True,
            errors="replace",
        )
        text = strings.stdout
        if "Xen Milenium" not in text:
            fail(f"binary does not contain the display name: {binary}")
        for token in FORBIDDEN:
            if token in text:
                fail(f"binary contains forbidden identifier {token!r}: {binary}")

    print(f"identity ok: {bundle}")


if __name__ == "__main__":
    main()
