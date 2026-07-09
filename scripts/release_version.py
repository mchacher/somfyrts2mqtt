"""
PlatformIO pre-script : derive `FW_VERSION` from `git describe`.

Replaces the previously hardcoded `-DFW_VERSION=\"0.1.0\"` build flag in
`platformio.ini` so the binary always embeds the exact git state :

  - on a tagged commit  : `0.2.0`               (from `v0.2.0`)
  - ahead of a tag      : `0.2.0-3-gabcd123`
  - dirty working tree  : `0.2.0-3-gabcd123-dirty`
  - no tag yet          : `gabcd123`            (short SHA fallback)
  - no git at all       : `0.0.0-unknown`

Wired in `[firmware_base]` via `extra_scripts = pre:scripts/release_version.py`.
The Status card in the admin UI reads this string straight from the binary,
so the displayed version always matches the running firmware -- no risk of
drift between the git tag and what the user sees.
"""

import subprocess

Import("env")  # noqa: F821  -- injected by PlatformIO


def _git_version() -> str:
    try:
        out = subprocess.check_output(
            [
                "git", "describe",
                "--tags",            # match annotated AND lightweight tags
                "--dirty",           # append "-dirty" if the worktree has changes
                "--always",          # fall back to a short SHA when no tag is reachable
                "--match", "v*",     # only release-style tags (skip "demo", etc.)
            ],
            stderr=subprocess.DEVNULL,
            text=True,
        ).strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        return "0.0.0-unknown"
    # "v0.2.0" -> "0.2.0" ; leave non-tag SHAs untouched.
    return out[1:] if out.startswith("v") else out


version = _git_version()
print(f"== firmware version : {version}")

# Quoted form for the C preprocessor : -DFW_VERSION=\"...\"
env.Append(BUILD_FLAGS=[f'-DFW_VERSION=\\"{version}\\"'])

# FW_VARIANT = the PlatformIO env name (e.g. "esp32-c3-mini", "esp32-s3-picybi").
# Same token that disambiguates the release .bin filename, so the Status card
# in the admin UI matches the file the user downloaded, and the WebOTA chip
# guard can name the board in its rejection message. Injected here (not via
# ${PIOENV} in platformio.ini) because ini interpolation of the env name is
# not reliable, whereas env["PIOENV"] in this pre-script always is.
variant = env["PIOENV"]
print(f"== firmware variant : {variant}")
env.Append(BUILD_FLAGS=[f'-DFW_VARIANT=\\"{variant}\\"'])
