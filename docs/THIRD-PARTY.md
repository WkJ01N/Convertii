# Third-party components

## FFmpeg / ffprobe

Pinned build: 8.0-full_build-www.gyan.dev. Exact binary SHA-256 values are in packaging/engine.lock.json. The publisher rejects mismatched binaries and --enable-nonfree.

- Build provider: https://www.gyan.dev/ffmpeg/builds/
- Provider-reported FFmpeg revision: https://github.com/FFmpeg/FFmpeg/commit/140fd653ae
- Redistribution guidance: https://ffmpeg.org/legal.html

Engine LICENSE, README/source details, actual build configuration and checksums are copied into each bundle. The engine includes optional libraries such as x264, libvpx, libwebp and libass. Their terms continue to apply.

Before a public binary release, archive and host the complete corresponding FFmpeg source, applicable dependency sources, patches, configuration and build materials. Upstream URLs and a provider README are not a complete source distribution.

## JSON for Modern C++ 3.12.0

Vendored under third_party/nlohmann. MIT license included.

- https://github.com/nlohmann/json/releases/tag/v3.12.0
- https://github.com/nlohmann/json/blob/v3.12.0/LICENSE.MIT

## miniz 3.1.2

Vendored under third_party/miniz, with the upstream LICENSE. The project supplies the static-build miniz_export.h definition.

- https://github.com/richgel999/miniz/releases/tag/3.1.2
- https://github.com/richgel999/miniz/blob/3.1.2/LICENSE

## Windows platform

Win32 common controls, Media Foundation, DirectComposition, CNG, WinHTTP and Windows package deployment are OS components. Convertii statically links the MSVC runtime; applicable Visual Studio redistribution terms apply.

The native release does not contain the .NET runtime, WPF, a browser runtime or a third-party playback framework.
