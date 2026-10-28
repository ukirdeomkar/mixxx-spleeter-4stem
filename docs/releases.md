# Building and publishing release artifacts

Release: <https://github.com/ukirdeomkar/mixxx-spleeter-4stem/releases/tag/v2.5.6-stems.1>

Status:

| Platform | Built by | Published by |
|---|---|---|
| Windows zip | `.github/workflows/build-stems.yml` (`windows` job) | same workflow (`publish` job) |
| Linux deb | `.github/workflows/build-stems.yml` (`linux` job) | same workflow (`publish` job) |
| macOS DMG | locally (see below) | manual upload |

`build-stems.yml` is `workflow_dispatch` only and builds both platforms in
parallel, then publishes them in **one** job. Give `release_tag` a tag and it
creates the release if the tag is new (`gh release create --prerelease`),
attaches every produced asset with `--clobber`, and then runs
`sync-downloads.yml` once to regenerate the README download table between the
`<!-- downloads:start -->` / `<!-- downloads:end -->` markers.

Leave `release_tag` empty for a build-only run (artifacts only). The
`build_windows` / `build_linux` inputs let you skip one platform; the publish
job still runs for whichever platform succeeded.

Security shape: both build jobs run with `permissions: contents: read`; only
the small `publish` job gets `contents: write`, and it does nothing but
download the artifacts and call `gh release upload`. It runs in the `release`
environment, so requiring a reviewer there gates all publishing behind a click
(Settings → Environments → release).

## macOS (Intel) — DMG

This is the only platform still built and uploaded by hand: Qt 6.5's `moc`
fails to parse `<concepts>` with SDKs newer than 12.1, so the runner image
cannot build it yet.

Requires the build from `README.md` (`-DMACOS_BUNDLE=ON`, macOS 12.1 SDK).

```bash
cd ~/mixxx-stems

# 1. Bundle install into dist/ (fixup_bundle copies non-system dylibs and
#    rewrites @rpath to @executable_path/../Frameworks, then ad-hoc signs)
mkdir -p models   # the four spleeter_*.onnx + NOTICE.txt
cmake -S . -B build -DMACOS_BUNDLE=ON
cmake --build build --parallel 2
cmake --install build --prefix "$PWD/dist"

# CMake installs models/*.onnx into Contents/Resources/models when ./models
# exists at configure time, so no manual copy is needed any more.

# 2. Re-sign, because files were added after the install-time signature
codesign --force --deep --sign - \
  --entitlements packaging/macos/Mixxx.entitlements dist/Mixxx.app

# 3. Stage and compress
rm -rf dist/dmg-stage && mkdir -p dist/dmg-stage
cp -R dist/Mixxx.app dist/dmg-stage/
ln -s /Applications dist/dmg-stage/Applications
# optional: dist/dmg-stage/"Read Me.txt"
hdiutil create -volname "Mixxx 2.5.6 Stems" \
  -srcfolder dist/dmg-stage -ov -format UDZO \
  dist/Mixxx-Stems-2.5.6-macOS-x86_64.dmg

# 4. Publish (see "Manual fallback" below; this machine has no gh CLI)
```

Checks before publishing:

```bash
hdiutil attach dist/Mixxx-Stems-2.5.6-macOS-x86_64.dmg -nobrowse -mountpoint /tmp/dmgcheck
codesign -v --deep --strict /tmp/dmgcheck/Mixxx.app
ls /tmp/dmgcheck/Mixxx.app/Contents/Resources/models   # 4 .onnx files
hdiutil detach /tmp/dmgcheck
```

Then run the app **from inside the bundle** once and confirm the log shows the
models loading from `Mixxx.app/Contents/Resources/models/...`.

## Windows (x64) — portable zip

`Actions -> "Build and publish (4-stem)" -> Run workflow`, `release_tag` set.

The workflow builds with MSVC on `windows-2022`, stages `mixxx.exe` + `res/` +
DLLs + the four models, and produces `mixxx-stems-windows-x64.zip`.

Gotchas, learned the hard way:

- GitHub artifacts are a **zip containing a zip**, and they require a GitHub
  login to download. The release asset is therefore the inner zip produced by
  the workflow, which has a public, anonymous download link.
- Mixxx's own ONNX auto-download breaks on CMake 3.21
  (`At least one entry of URL is a path`), so the workflow downloads ONNX
  Runtime 1.19.2 itself and passes `ONNXRUNTIME_INCLUDE_DIR` /
  `ONNXRUNTIME_LIBRARY` with `MIXXX_AUTO_DOWNLOAD_ONNX=OFF`.
- `Ort::Session` needs a `const wchar_t*` path on Windows (`toStdWString()`),
  UTF-8 elsewhere. Passing `toUtf8()` only fails on MSVC.
- The zip must keep its folder layout: `mixxx.exe` needs `onnxruntime.dll`,
  `fdk-aac.dll`, `models/`, `controllers/`, `platforms/`, `skins/` next to it.

## Linux — deb

`Actions -> "Build and publish (4-stem)" -> Run workflow`, `release_tag` set.

The workflow uses Mixxx's `tools/debian_buildenv.sh` on `ubuntu-22.04`, builds
with FFmpeg off, and packages with `cpack -G DEB`. The models are installed to
`/usr/share/mixxx/models`.

Gotchas:

- Debian/Ubuntu ship `libfftw3f` but no `FFTW3fConfig.cmake`; CMakeLists falls
  back to `find_library(fftw3f)`.
- ONNX Runtime is not a distro package. The CMake install rule puts the real
  library (symlinks resolved) at `/usr/lib/mixxx/libonnxruntime.so.1` and sets
  `INSTALL_RPATH=$ORIGIN/../lib/mixxx`.
- `dpkg-shlibdeps` must be told where to find it via
  `CPACK_DEBIAN_PACKAGE_SHLIBDEPS_PRIVATE_DIRS` (a **directory**). That variable
  has to be set **before `include(CPack)`** or it never reaches
  `CPackConfig.cmake`.

## Manual fallback (no `gh` CLI)

The API works with the token git already stores in the macOS keychain:

```bash
TOKEN=$(printf "protocol=https\nhost=github.com\n" | git credential fill | sed -n 's/^password=//p')
```

```bash
# create (once per version)
curl -s -X POST -H "Authorization: token $TOKEN" \
  https://api.github.com/repos/ukirdeomkar/mixxx-spleeter-4stem/releases \
  -d '{"tag_name":"v2.5.6-stems.1","target_commitish":"main",
       "name":"...","prerelease":true,"body":"..."}'

# upload an asset. To replace an existing one, delete it first:
#   GET  .../releases/<ID>/assets   -> find asset id
#   DELETE .../releases/assets/<asset_id>
curl -s -X POST -H "Authorization: token $TOKEN" \
  -H "Content-Type: application/octet-stream" \
  --data-binary "@dist/Mixxx-Stems-2.5.6-macOS-x86_64.dmg" \
  "https://uploads.github.com/repos/ukirdeomkar/mixxx-spleeter-4stem/releases/<ID>/assets?name=Mixxx-Stems-2.5.6-macOS-x86_64.dmg"

# then refresh the README table
Actions -> "Sync download links" -> tag v2.5.6-stems.1
```

## TODO

- Get macOS into CI. Upstream Mixxx builds macOS on `macos-13`/`macos-15-intel`
  with its release buildenv; worth trying before pinning the 12.1 SDK.
- Once every platform comes from one tag, point the README links at
  `/releases/latest` instead of a fixed tag.
