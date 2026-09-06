"""Inject build identity into the firmware as -D flags.

Nobody types a version into a header by hand: they forget on the second
release and then "1.4.2" exists in five different variants. Everything here
comes from git, and a build with uncommitted changes is marked -dirty so it
can be refused at release time.

Wired up from platformio.ini as:  extra_scripts = pre:scripts/version_flags.py
"""

import datetime
import os
import subprocess

Import("env")  # noqa: F821  (SCons injects this)

# Used when the repository has no v-tag yet. The tag always wins when present.
FALLBACK_SEMVER = "1.0.0"

PROJECT_DIR = env["PROJECT_DIR"]  # noqa: F821


def git(*args, default=""):
    """Run a git command inside the project dir; never fail the build."""
    try:
        out = subprocess.run(
            ["git"] + list(args),
            cwd=PROJECT_DIR,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            check=True,
        )
        return out.stdout.decode("utf-8", "replace").strip()
    except (subprocess.CalledProcessError, OSError):
        return default


def semver_from_tag():
    """Nearest v-tag as a bare semver, or the fallback."""
    tag = git("describe", "--tags", "--abbrev=0", "--match", "v[0-9]*")
    if not tag:
        print("version_flags: no v-tag found, falling back to %s" % FALLBACK_SEMVER)
        return FALLBACK_SEMVER
    return tag[1:] if tag.startswith("v") else tag


def is_dirty():
    """Dirty is scoped to this project, not the whole monorepo.

    The repo also holds unrelated projects; their edits must not mark this
    firmware dirty, or the flag stops meaning anything.
    """
    return bool(git("status", "--porcelain", "--", PROJECT_DIR))


semver = semver_from_tag()
git_hash = git("rev-parse", "--short=7", "HEAD", default="nogit00")
dirty = 1 if is_dirty() else 0
build_utc = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")

# The panic record and the health telemetry frame carry the hash as a number:
# there is no room for a string in either.
try:
    hash_u32 = int(git_hash, 16)
except ValueError:
    hash_u32 = 0

stringify = getattr(env, "StringifyMacro", lambda v: '\\"%s\\"' % v)  # noqa: F821

# The hardware id the node reports and the one an update manifest is checked
# against. Taken from the board definition rather than typed, and taken from the
# same place scripts/release_manifest.py takes it, because these two strings
# disagreeing is how a node accepts an image built for a different pinout.
hw_id = env.BoardConfig().get("build.variant", "unknown")  # noqa: F821

# The fleet API key, if the person doing the build has one in their
# environment. It is a secret, so it is never in platformio.ini and never in
# git; a build without it produces an image whose key is empty, and the node is
# given one in the field with `net set key <key>`.
api_key = os.environ.get("LORA_FLEET_API_KEY", "")
if api_key:
    print("version_flags: baking LORA_FLEET_API_KEY into this image")

env.Append(  # noqa: F821
    CPPDEFINES=[
        ("FW_SEMVER", stringify(semver)),
        ("FW_GIT_HASH", stringify(git_hash)),
        ("FW_BUILD_UTC", stringify(build_utc)),
        ("FW_GIT_DIRTY", dirty),
        ("FW_HASH_U32", "0x%08xu" % hash_u32),
        ("FW_HW_ID", stringify(hw_id)),
    ]
    + ([("NET_DEFAULT_KEY", stringify(api_key))] if api_key else [])
)

print(
    "version_flags: %s+%s%s  built %s  hw %s"
    % (semver, git_hash, "-dirty" if dirty else "", build_utc, hw_id)
)
