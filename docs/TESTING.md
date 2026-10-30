# Native validation and release checklist

Run scripts/Test.ps1. CMake builds the native application, installer, core and test executables. CTest runs four suites without installing or removing system registrations.

## Automated

- Core: real FFmpeg-generated media; strict byte caps and full/edited duration, no overwrite, original hash preservation, video formats, default/selected tracks, images and alpha, HDR consent and tone mapping, EXIF rotation, animated/corrupt input, cancellation, blocked output and cleanup.
- Extension: MP3/M4A/FLAC/WAV and standalone audio, combined trim/crop/rotation/speed/gain, edited size budget, remux and copy/filter conflicts, fast trim, snapshots, subtitle timeline/burn/mux/extraction, mismatched and normalized concat, missing-audio silence, preview generation/cancellation/cache and settings migration.
- Hardware: a test-only native process simulates successful device detection followed by device loss; actual software encoding must complete afterward.
- UI: standard Win32 controls in both languages, parameter round trip and job snapshots, asynchronous probe, per-file/confirmation state, timeline/playback lifecycle, request path checks and 4,000-file request/pipe handoff. Test windows are transparent, off screen and non-activating; screenshots are written to artifacts/native-ui-tests.
- Installer: exact checksum, incomplete/unsafe/duplicate ZIP paths, personal-file refusal, owned updates, integration-failure rollback, dry-run uninstall, locked-file preflight and ownership-only deletion. OS integration is replaced with a test callback.
- Shell: real native COM activation, both menu interfaces, submenu enumeration, DLL lifetime and 4,000-file Unicode selection delivery to a test-only receiver. The receiver is never included in release bundles.

Playback availability and native UI startup/private-memory measurements are recorded separately in UI test metrics. A missing platform playback component must not be silently counted as successful playback.

## Windows 10 22H2 / Windows 11 x64 manual validation

- Use a clean machine without .NET. Check first launch, offline media processing, file picker and drag/drop. Inspect application imports and release contents for managed runtime files.
- Check both languages at 100%, 125%, 150% and 200% DPI, monitor changes, high contrast, keyboard focus/mnemonics and scrollable forms at smaller sizes.
- Install full bundle offline; download through the online installer using the real hosted HTTPS URL; interrupt, corrupt and retry the download. Check that another release ZIP is rejected.
- Check portable register/repair/remove, directory relocation, update and uninstall. On Windows 11 verify both modern and Show more options entries. Disable/re-enable modern entry. Test an untrusted identity package and classic fallback.
- Select 1 / 20 / 1,000 files, Chinese and spaces, duplicate and long names. Reinvoke while a batch is running. Check that queued parameters remain stable.
- Test actual NVIDIA, Intel and AMD hosts as well as software-only operation. A simulated device-failure test does not replace vendor/driver validation.
- Long/VFR/rotated/anamorphic recordings, multiple tracks, HDR10/HLG, portrait video and image EXIF/alpha. Compare geometry, color and audio/subtitle sync visually.
- Confirm keyframe trim is clearly distinguished from precise trim. Check range marking, proxy time mapping, effect samples, stale-request cancellation, cache eviction and shutdown during preview.
- Test matching fast concat, differing codecs/resolutions/frame rates, clip ranges, missing audio, normalized target-size output and the summary of omitted tracks.
- Full disk, access denied, deleted input, missing Media Feature Pack, input codec unavailable to Windows, locked Shell DLL and close while running. No original changes or completed-looking partial outputs.
- Update/uninstall while Explorer holds the DLL and while the app is open. Personal files and settings survive failed operations. A blocked rollback must preserve the previous directory for recovery.

## Public release gates

Use a trusted signing certificate with publisher metadata matching the application and identity package. Never automatically trust a developer certificate. Configure actual download URLs, publish complete corresponding engine/dependency sources and build materials, retain all license notices, sign releases and publish checksums.

Local native tests and checksum verification do not establish Windows 10/11 clean-VM or real Explorer registration coverage. Record those results before marking a public release ready.
