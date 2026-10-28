# Building and publishing release artifacts

Current state: **manual, one-off**. The CI builds code but does not publish
anything. This file records exactly how the artifacts on the release page were
produced so it can be repeated, and what should be automated later.

Release: <https://github.com/ukirdeomkar/mixxx-spleeter-4stem/releases/tag/v2.5.6-stems.1>

## macOS (Intel) — DMG

Requires the build from `README.md` (`-DMACOS_BUNDLE=ON`, macOS 12.1 SDK).

```bash
cd ~/mixxx-stems

# 1. Bundle install into dist/ (fixup_bundle copies non-system dylibs and
#    rewrites @rpath to @executable_path/../Frameworks, then ad-hoc signs)
cmake -S . -B build -DMACOS_BUNDLE=ON
cmake --build build --parallel 2
cmake --install build --prefix "$PWD/dist"

# 2. The ONNX models are not known to Mixxx's install rules - add them
mkdir -p dist/Mixxx.app/Contents/Resources/models
cp ~/Library/Application\ Support/Mixxx/models/spleeter_*.onnx \
   dist/Mixxx.app/Contents/Resources/models/

# 3. Re-sign, because files were added after the install-time signature
codesign --force --deep --sign - \
  --entitlements packaging/macos/Mixxx.entitlements dist/Mixxx.app

# 4. Stage and compress
rm -rf dist/dmg-stage && mkdir -p dist/dmg-stage
cp -R dist/Mixxx.app dist/dmg-stage/
ln -s /Applications dist/dmg-stage/Applications
# optional: dist/dmg-stage/"Read Me.txt"
hdiutil create -volname "Mixxx 2.5.6 Stems" \
  -srcfolder dist/dmg-stage -ov -format UDZO \
  dist/Mixxx-Stems-2.5.6-macOS-x86_64.dmg
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

The build runs in GitHub Actions (`.github/workflows/windows-build.yml`),
manually triggered:

```
Actions -> "Windows build (4-stem)" -> Run workflow (branch: main)
```

The workflow builds with MSVC on `windows-2022`, stages `mixxx.exe` + `res/` +
DLLs + the four models, and uploads the artifact `mixxx-stems-windows-x64`.

Gotchas, learned the hard way:

- GitHub artifacts are a **zip containing a zip**, and they require a GitHub
  login to download. Download the artifact, extract the outer zip, and publish
  the inner `mixxx-stems-windows-x64.zip` to the release so it has a public,
  anonymous download link.
- Mixxx's own ONNX auto-download breaks on CMake 3.21
  (`At least one entry of URL is a path`), so the workflow downloads ONNX
  Runtime 1.19.2 itself and passes `ONNXRUNTIME_INCLUDE_DIR` /
  `ONNXRUNTIME_LIBRARY` with `MIXXX_AUTO_DOWNLOAD_ONNX=OFF`.
- `Ort::Session` needs a `const wchar_t*` path on Windows (`toStdWString()`),
  UTF-8 elsewhere. Passing `toUtf8()` only fails on MSVC.
- The zip must keep its folder layout: `mixxx.exe` needs `onnxruntime.dll`,
  `fdk-aac.dll`, `models/`, `controllers/`, `platforms/`, `skins/` next to it.

## Publishing (manual, via the GitHub API)

No `gh` CLI on the build machine; the API is used with the token git already
stores in the macOS keychain:

```bash
TOKEN=$(printf "protocol=https\nhost=github.com\n" | git credential fill | sed -n 's/^password=//p')
```

1. Create the release (once per version):

   ```bash
   curl -s -X POST -H "Authorization: token $TOKEN" \
     https://api.github.com/repos/ukirdeomkar/mixxx-spleeter-4stem/releases \
     -d '{"tag_name":"v2.5.6-stems.1","target_commitish":"main",
          "name":"...","prerelease":true,"body":"..."}'
   ```

2. Upload each asset:

   ```bash
   curl -s -X POST -H "Authorization: token $TOKEN" \
     -H "Content-Type: application/octet-stream" \
     --data-binary "@dist/Mixxx-Stems-2.5.6-macOS-x86_64.dmg" \
     "https://uploads.github.com/repos/ukirdeomkar/mixxx-spleeter-4stem/releases/<ID>/assets?name=Mixxx-Stems-2.5.6-macOS-x86_64.dmg"
   ```

3. Edit the release notes with `PATCH .../releases/<ID>`.

Note: a `v*` tag push also triggers the Windows workflow (by design). If the tag
is created before the code is final, cancel that run instead of letting it build
the wrong commit.

## TODO — automate

- Add a `release` workflow: build macOS (pinned SDK) and Windows, then upload
  the DMG and zip to the release an existing tag points at, using
  `GITHUB_TOKEN` (`permissions: contents: write`).
- macOS on GitHub runners currently needs the SDK pinning solved first; until
  then the DMG is produced locally.
- Only then move the "Download" links in `README.md` from the fixed
  `v2.5.6-stems.1` URLs to the `/releases/latest` redirect.
