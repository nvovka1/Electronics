"""Inject build identity into the firmware as -D flags.

Build identity is two separate facts and they come from different places on
purpose:

  the semver   what this release CLAIMS about compatibility. A human decision,
               so a human sets it: custom_fw_version in platformio.ini. Falling
               back to the nearest v-tag when it is left empty.

  the hash     which code it ACTUALLY is, plus whether the tree was clean.
               Never typed, always from git, because this is the half that has
               to be true rather than intended.

That split is what makes `1.1.1+9c41b7e` worth printing: the left half says
what was meant and the right half proves what was built. A version typed into a
header would give you the first without the second, and then "1.4.2" exists in
five variants with no way to tell them apart.

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


def resolve_semver():
    """custom_fw_version if it is set, otherwise the nearest v-tag.

    Set by hand because what a version number CLAIMS - that this release is
    compatible with that one - is a judgement no tool can make. The tag remains
    the fallback so a checkout with neither still builds.
    """
    configured = env.GetProjectOption("custom_fw_version", "").strip()  # noqa: F821
    if not configured:
        return semver_from_tag(), "v-tag"

    tag = git("describe", "--tags", "--abbrev=0", "--match", "v[0-9]*")
    tag_semver = tag[1:] if tag.startswith("v") else tag

    # Said out loud rather than silently preferred. Shipping 1.1.1 from a tree
    # tagged v1.1.0 is a perfectly normal thing to do while a release is being
    # prepared - and it is also exactly what a forgotten tag looks like.
    if tag_semver and tag_semver != configured:
        print(
            "version_flags: custom_fw_version is %s but the nearest tag is v%s; "
            "using %s" % (configured, tag_semver, configured)
        )

    return configured, "custom_fw_version"


def is_dirty():
    """Dirty is scoped to this project, not the whole monorepo.

    The repo also holds unrelated projects; their edits must not mark this
    firmware dirty, or the flag stops meaning anything.
    """
    return bool(git("status", "--porcelain", "--", PROJECT_DIR))


semver, semver_source = resolve_semver()
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

# The fleet API key. A secret, so it is never in platformio.ini and never in
# git; a build without it produces an image whose key is empty, and the node is
# given one in the field with `net set key <key>`.
#
# Two sources, environment first so a one-off build can override the file
# without editing it:
#
#   1. the LORA_FLEET_API_KEY environment variable
#   2. secrets.local.ini next to platformio.ini, which is gitignored
#
# The file exists so the key is set once rather than exported into every shell,
# which is the mistake that ends with somebody putting it in platformio.ini.
def read_local_secret(name):
    """Reads NAME from secrets.local.ini. Missing file is not an error - most
    builds do not want a key baked in at all."""
    path = os.path.join(env.subst("$PROJECT_DIR"), "secrets.local.ini")  # noqa: F821
    if not os.path.isfile(path):
        return ""

    with open(path, "r", encoding="utf-8") as handle:
        for raw in handle:
            line = raw.strip()
            if not line or line.startswith("#") or line.startswith(";"):
                continue
            if "=" not in line:
                continue
            found, _, value = line.partition("=")
            if found.strip() == name:
                # Quotes are stripped because everybody types them eventually,
                # and a key wrapped in quotes fails authentication in a way
                # that looks exactly like a wrong key.
                return value.strip().strip('"').strip("'")
    return ""


# Three sources, highest first. Exactly one -D is emitted, from here, so the
# macro can never be defined twice with different values.
#
#   1. LORA_FLEET_API_KEY in the environment   - a one-off build
#   2. secrets.local.ini, gitignored           - a real fleet, key kept out of git
#   3. custom_fleet_api_key in platformio.ini  - the demo fleet, in plain sight
#
api_key = (
    os.environ.get("LORA_FLEET_API_KEY", "")
    or read_local_secret("LORA_FLEET_API_KEY")
    or env.GetProjectOption("custom_fleet_api_key", "")  # noqa: F821
)

if api_key:
    print("version_flags: fleet API key compiled in (%d chars)" % len(api_key))
else:
    print(
        "version_flags: no fleet API key in this image - nodes will need "
        "`net set key <key>` before the service accepts them"
    )

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
    "version_flags: %s+%s%s  built %s  hw %s  (semver from %s)"
    % (semver, git_hash, "-dirty" if dirty else "", build_utc, hw_id, semver_source)
)

if dirty:
    # Named, not just flagged. "It says dirty and I do not know why" is a
    # question this script can simply answer.
    changed = git("status", "--porcelain", "--", PROJECT_DIR).splitlines()
    print(
        "version_flags: WORKING TREE IS DIRTY - %d changed path%s, so this image "
        "cannot be reproduced from any commit and the fleet service will refuse "
        "to deploy it. Commit them to clear it." % (len(changed), "" if len(changed) == 1 else "s")
    )
    for line in changed[:8]:
        print("version_flags:   %s" % line)
    if len(changed) > 8:
        print("version_flags:   ... and %d more" % (len(changed) - 8))
