"""Write a manifest next to the built image.

A .bin on its own is anonymous. Under the Arduino framework the ESP-IDF app
descriptor baked into the image at offset 0x20 belongs to the *framework
builder*, not to us - it reads "arduino-lib-builder / esp-idf: v4.4.7" no
matter what we compile. So anything that needs to know which firmware a file
contains has to be told separately.

That is what this manifest is for: version, git hash, build time, size,
SHA-256 and hardware id, in a file that sits next to the .bin. A flashing tool
or a gateway reads it to decide whether to write the image at all.

It is deliberately not burnt into eFuses. eFuse bits only ever go from 0 to 1,
so a version stored there could never be corrected, would consume a fixed
budget on every release, and would become a second source of truth that can
disagree with the code actually running.

Wired up from platformio.ini as: extra_scripts = post:scripts/release_manifest.py
"""

import hashlib
import json
import os

Import("env")  # noqa: F821


def describe(defines, name, default=""):
    """Pull a -D value back out of the SCons environment."""
    for item in defines:
        if isinstance(item, (list, tuple)) and len(item) == 2 and item[0] == name:
            return str(item[1]).strip('\\"')
    return default


def write_manifest(source, target, env):  # noqa: ARG001
    firmware = str(target[0])
    if not os.path.exists(firmware):
        return

    with open(firmware, "rb") as handle:
        image = handle.read()

    defines = env.get("CPPDEFINES", [])

    manifest = {
        "name": "lora-queue-release",
        "version": describe(defines, "FW_SEMVER"),
        "git_hash": describe(defines, "FW_GIT_HASH"),
        "dirty": bool(int(describe(defines, "FW_GIT_DIRTY", "1"))),
        "build_utc": describe(defines, "FW_BUILD_UTC"),
        "build_type": describe(defines, "FW_BUILD_TYPE", env["PIOENV"]),
        "proto_version": int(describe(defines, "PROTO_VERSION", "1")),
        # Two boards with different pinouts and the same connector is how an
        # image from a neighbouring revision turns a device into a brick.
        "hw_id": env.BoardConfig().get("build.variant", "unknown"),
        "board": env.BoardConfig().get("name", env["BOARD"]),
        "size_bytes": len(image),
        "sha256": hashlib.sha256(image).hexdigest(),
    }

    path = os.path.join(env.subst("$BUILD_DIR"), "manifest.json")
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(manifest, handle, indent=2)
        handle.write("\n")

    flag = " DIRTY - do not deploy" if manifest["dirty"] else ""
    print(
        "manifest: %s+%s %s  %d bytes  sha256 %s...%s"
        % (
            manifest["version"],
            manifest["git_hash"],
            manifest["build_type"],
            manifest["size_bytes"],
            manifest["sha256"][:8],
            flag,
        )
    )


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", write_manifest)  # noqa: F821
