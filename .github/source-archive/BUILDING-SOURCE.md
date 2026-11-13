# Building this source archive

The product files come from the exact revision in SOURCE-MANIFEST.json.
Both Signalsmith dependencies are included under third-party at pinned revisions.
Their original MIT licenses are in each dependency's LICENSE.txt.
The product GPL license is at LICENSE. No dependency source was modified.
The upstream Stretch cmd/util submodule is not included: it is used by the
separate CLI example, not by this product. Product CMake disables submodule
fetching. Upstream sample media is retained as part of the dependency archive.

## Windows

Use CMake 3.28 or later, Visual Studio 2022 with C++, and the Windows SDK
specified by CMakePresets.json. From this extracted directory, run in PowerShell:

```powershell
cmake --preset windows-x64 "-DFETCHCONTENT_SOURCE_DIR_SIGNALSMITH-STRETCH=$((Get-Location).Path)/third-party/signalsmith-stretch" "-DFETCHCONTENT_SOURCE_DIR_SIGNALSMITH-LINEAR=$((Get-Location).Path)/third-party/signalsmith-linear"
cmake --build --preset windows-x64
cmake --install build_x64 --config RelWithDebInfo --prefix dist
```

These overrides select included source trees without downloading or updating
them. Hyphens are part of the dependency names and must remain in the variables.
Reference: cmake --help-module FetchContent, FETCHCONTENT_SOURCE_DIR_<uppercaseName>.

## Other platforms and remaining dependencies

Use the macos or ubuntu-x86_64 presets with the same two source overrides and
absolute paths appropriate to your shell. CI builds passed for the integration
candidate; macOS and Linux runtime testing has not been performed.

This is not a fully offline SDK. buildspec.json records the OBS source,
prebuilt obs-deps, and Qt versions, download URLs, and hashes. Existing
cmake/windows/buildspec.cmake and common buildspec helpers obtain the Windows
dependencies during configuration. Platform CMake helpers and .github workflows
describe other platform setup. Qt is not linked by the default audio-filter
target; its download can still occur during template dependency setup.
A network connection and the required platform toolchain may be needed.
