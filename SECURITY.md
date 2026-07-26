# Security Policy

## About this fork

This is an **experimental, community-maintained source fork** of Vanilla Conquer containing in-progress iOS (arm64) portability work. It is **not affiliated with or endorsed by** Electronic Arts, Westwood Studios, Apple, or the upstream TheAssemblyArmada/Vanilla-Conquer project. Command & Conquer, Tiberian Dawn, and Red Alert are trademarks of their respective owners; references are descriptive only.

The repository provides **source code only**. It contains **no commercial game data**. You must supply your own lawfully obtained, compatible game data. Do not use unofficial repacks.

## Reporting a vulnerability

Please report security issues **privately** — do not open a public issue for a suspected vulnerability.

1. Preferred: use **GitHub Private Vulnerability Reporting** for this repository (Security → *Report a vulnerability*). *Maintainer: enable this under Settings → Code security → Private vulnerability reporting.*
2. If private reporting is unavailable, open a minimal public issue that says only that you have a security report and requests a private contact channel — **do not include details, logs, or proof-of-concept in the public issue.**

Please do not include credentials, personal data, or unredacted diagnostic logs in any report. We aim to acknowledge reports within a reasonable time; this is a hobbyist project with no guaranteed response SLA.

## Supported versions

This is pre-release experimental work. Only the current `vanilla` branch tip is worked on. Older commits and any downloadable build artifacts are **not** maintained or patched.

## Build artifacts and trust model

- CI-produced iOS `.app` bundles are **experimental, unsigned, debug builds**. They are **not** signed releases and should not be treated as trusted software.
- Artifacts are assembled from external dependencies (SDL2, openal-soft). Treat any artifact's provenance as only as strong as its recorded `BUILDINFO.txt` and published checksums.
- Do not install build artifacts unless you understand and accept these risks. Prefer building from source you have reviewed.

## Diagnostic logging and safe log sharing

- Experimental builds may enable verbose engine diagnostics that write to `Documents/vcengine.txt` on device. These logs can contain **local network addresses, file paths, and gameplay state**.
- Before sharing a log for support, **redact** local IP addresses, hostnames, usernames, and file paths. Never paste raw logs into public forums.

## Networking (multiplayer) trust boundary

- Local multiplayer is **trusted-local-LAN experimental functionality only**. It uses unauthenticated local discovery (Bonjour `_vctd._tcp`) and legacy UDP (port 1234) with no peer authentication or integrity protection.
- Use it **only** on networks you trust. Do **not** use it on public, guest, enterprise, or otherwise untrusted networks. Any device on the same network may send traffic to the game's parsers.

## User-supplied files

The iOS builds enable file sharing / open-in-place. Legacy parsers assume trusted input. Only import save, configuration, and data files from sources you trust; malformed or hostile files may not be handled safely.

## Scope and limitations

This policy documents known boundaries of an experimental fork; it is not a warranty or a claim that the software is secure. See `License.txt` (GPLv3 with additional terms) for licensing, and the README for build and data requirements.
