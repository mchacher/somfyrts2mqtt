# Releasing (maintainer)

How a new firmware version reaches end users. Most of it is automated ; the maintainer only tags + pushes.

## Versioning model

The displayed firmware version comes from `git describe --tags --dirty --always --match "v*"` at build time, evaluated by `scripts/release_version.py` :

| Git state | `FW_VERSION` |
|---|---|
| On a tagged commit (`v0.2.0`) | `0.2.0` (the leading `v` is stripped) |
| 3 commits past `v0.2.0` | `0.2.0-3-gabcd123` |
| Same as above, with uncommitted changes | `0.2.0-3-gabcd123-dirty` |
| No tag in history | `gabcd123` (short SHA fallback) |
| Git unavailable | `0.0.0-unknown` |

The Status card in the admin UI reads this string straight from the binary, so the displayed version always matches the running firmware.

There is **no `FW_VERSION` constant to bump manually** in `platformio.ini` -- the script handles it. The single source of truth is the git tag.

## Cut a release

The repo follows semver-ish (`vMAJOR.MINOR.PATCH`).

```bash
# 1. Make sure main is clean and CI is green
git checkout main
git pull

# 2. Tag the release (annotated, so it shows up in `git describe`)
git tag -a v0.2.0 -m "v0.2.0 - <short tagline>"

# 3. Push the tag (the workflow listens to push events on refs/tags/v*)
git push origin v0.2.0
```

Within ~2 minutes :
- `.github/workflows/release.yml` runs
- Builds `firmware.bin` for **both** envs (`esp32-c3-mini` and `esp32-s3-picybi`)
- Renames each to `somfyrts2mqtt-0.2.0-<env>.bin`
- Computes the SHA256 sums
- Creates a GitHub Release at `/releases/tag/v0.2.0`
- Attaches both `.bin` files and the `sha256sums.txt`
- Auto-generates a release body from PR titles + linked issues since the previous tag, categorised per `.github/release.yml` (Features / Fixes / Documentation / etc.)

End users go to <https://github.com/mchacher/somfyrts2mqtt/releases/latest>, download **the binary matching their board** (C3 vs S3 — the two are not interchangeable) and upload it via the bridge's web UI → Update firmware. The WebOTA handler rejects a binary built for the wrong chip, so a mismatch fails safely instead of bricking. No source-code checkout or PlatformIO needed on their side.

## Categorised release notes (PR labels)

The auto-generated body uses PR labels for grouping. Configure each PR (or label retroactively before tagging) with one of :

| Label | Group |
|---|---|
| `feature`, `enhancement`, `feat` | 🚀 Features |
| `fix`, `bug` | 🐛 Fixes |
| `docs`, `documentation` | 📚 Documentation |
| `ci`, `build` | 🔧 CI / build |
| `refactor`, `chore` | ♻️ Refactor / chore |
| `test`, `tests` | 🧪 Tests |
| Anything else | Other changes |

PRs with the `skip-changelog` or `dependencies` label are excluded entirely (dependabot noise stays out of the release body).

The mapping lives in `.github/release.yml`. Add new labels there if you adopt new conventions.

## Pre-releases

For a release candidate or a beta, use the standard `-rcN` / `-betaN` suffix in the tag :

```bash
git tag -a v0.2.0-rc1 -m "v0.2.0 release candidate 1"
git push origin v0.2.0-rc1
```

GitHub automatically flags `*-rc*`, `*-beta*`, `*-alpha*` tags as **pre-releases** ; they do NOT appear at `/releases/latest`. End users only get them if they navigate to the full release list.

The binaries are still built and attached -- pre-releases are a perfectly fine OTA target for early testers.

## Recover from a botched release

The release workflow does **not** push back to `main` -- nothing is modified after the build except the GitHub Release page. To withdraw a release :

```bash
# Delete the GitHub release (web UI : releases > pick > Delete release)
gh release delete v0.2.0 --repo mchacher/somfyrts2mqtt --yes

# Delete the tag locally and on remote
git tag -d v0.2.0
git push origin :refs/tags/v0.2.0

# Fix whatever was wrong, then tag again with a bumped patch number (v0.2.1)
```

Re-using the same tag number after deletion is technically possible but confusing for users who already downloaded the .bin -- bump to the next patch instead.

## Hotfix workflow

For a critical fix that needs to ship without the next batch of features :

1. Cut a `fix/...` branch off main, fix, PR, label `fix`, merge.
2. Tag the merge commit as `v0.2.1`, push.
3. Existing OTA mechanism rolls the fix out to anyone who clicks Upload in the admin UI.

Auto-update from a release feed (poll + notify) is not implemented yet. End users still pull updates manually -- low friction since the admin UI link goes straight to `/releases/latest`.
