#!/bin/bash
# Build, sign, notarize and staple the Audio to MIDI installer.
#
# Modelled on EQ's package.sh (same identities, same steps, same checks), with
# what this plugin adds: a standalone app, the preview's soundfont, and Apple
# Silicon only.
#
#   ./package.sh                 # version read from CMakeLists.txt
#   ./package.sh --skip-notarize # everything but the Apple round-trip (a test build)
#   ./package.sh --no-release    # build and notarize, but cut no GitHub release
#
# The output lands in dist/. A release is also tagged and published to GitHub
# with the installer attached, so the site's download and GitHub's cannot drift.
#
# The installer puts:
#   Audio to MIDI.component  -> /Library/Audio/Plug-Ins/Components   (choice)
#   Audio to MIDI.vst3       -> /Library/Audio/Plug-Ins/VST3         (choice)
#   Audio to MIDI.app        -> /Applications                         (choice)
#   GeneralUser-GS.sf2       -> /Library/Application Support/AudioFlower/soundfonts
#                               (always: the preview's sounds, see Engine/SoundFont)
# The transcription model is NOT in it: the plugin downloads it on first use.
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

BUILT="build/Release/AudioToMidi_artefacts/Release"
NAME="Audio to MIDI"
NOTARY_PROFILE="${NOTARY_PROFILE:-notarytool}"   # EQ's; see its package.sh
GH_REPO="lukeglad/audio-to-midi"

# Pinned by SHA-1, not by name: the keychain holds TWO "Developer ID Installer"
# certificates with the same name (EQ's package.sh explains). These are the
# ones that signed EQ 1.0.0.
APP_IDENTITY="2A9ECB08D03653B57376AA7BD16667029C963A6B"       # Developer ID Application
INSTALLER_IDENTITY="200FC63D7157C85532BA418CDA95CF1833D4288D" # Developer ID Installer

# The soundfont the installer carries, checked against the same pin as build.sh.
SF_FILE="$HOME/Library/Application Support/AudioFlower/soundfonts/GeneralUser-GS.sf2"
SF_SHA256=$(grep -oE '^SF_SHA256="[0-9a-f]{64}"' build.sh | cut -d'"' -f2)

SKIP_NOTARIZE=0
NO_RELEASE=0
for arg in "$@"; do
  case "$arg" in
    --skip-notarize) SKIP_NOTARIZE=1 ;;
    --no-release)    NO_RELEASE=1 ;;
    *) echo "unknown argument: $arg" >&2; exit 2 ;;
  esac
done
# An unnotarized build is a test artifact, never a release.
[ "$SKIP_NOTARIZE" -eq 1 ] && NO_RELEASE=1

VERSION=$(grep -oE '^project \(AudioToMidi VERSION [0-9]+\.[0-9]+\.[0-9]+' CMakeLists.txt | grep -oE '[0-9.]+$')
[ -n "$VERSION" ] || { echo "could not read the version from CMakeLists.txt" >&2; exit 1; }
PLUGIN_VERSION=$(grep -oE '^    VERSION +[0-9]+\.[0-9]+\.[0-9]+' CMakeLists.txt | grep -oE '[0-9.]+$')
[ "$PLUGIN_VERSION" = "$VERSION" ] \
  || { echo "CMakeLists.txt disagrees with itself: project $VERSION, plugin $PLUGIN_VERSION" >&2; exit 1; }

OUT="dist"
# A test build is named so it can never be mistaken for (or copied as) a release.
if [ "$SKIP_NOTARIZE" -eq 1 ]; then
  PKG="$OUT/Audio-to-MIDI-$VERSION-UNNOTARIZED.pkg"
else
  PKG="$OUT/Audio-to-MIDI-$VERSION.pkg"
fi
say() { printf '\n\033[1m==> %s\033[0m\n' "$1"; }

# ------------------------------------------------------------------ preflight
say "Preflight"
for id in "$APP_IDENTITY" "$INSTALLER_IDENTITY"; do
  security find-identity -v | grep -q "$id" \
    || { echo "signing identity $id is not in the keychain" >&2; exit 1; }
done
echo "  both signing identities present"
for f in LICENSE THIRD-PARTY-NOTICES.md packaging/terms.txt packaging/standalone.entitlements; do
  [ -f "$f" ] || { echo "$f missing" >&2; exit 1; }
done
[ -n "$SF_SHA256" ] || { echo "could not read SF_SHA256 from build.sh" >&2; exit 1; }

if [ "$SKIP_NOTARIZE" -eq 0 ]; then
  xcrun notarytool history --keychain-profile "$NOTARY_PROFILE" >/dev/null 2>&1 || {
    echo "no notary profile '$NOTARY_PROFILE'." >&2
    echo "create one once with:" >&2
    echo "  xcrun notarytool store-credentials $NOTARY_PROFILE \\" >&2
    echo "    --apple-id <your-apple-id> --team-id V9722FBXP2 --password <app-specific-password>" >&2
    echo "or re-run with --skip-notarize to test everything else." >&2
    exit 1
  }
  echo "  notary profile '$NOTARY_PROFILE' works"
fi

if [ "$NO_RELEASE" -eq 0 ]; then
  command -v gh >/dev/null || { echo "gh is not installed -- pass --no-release" >&2; exit 1; }
  gh auth status >/dev/null 2>&1 || { echo "gh is not authenticated -- pass --no-release" >&2; exit 1; }
  # The release says "check out this tag and run package.sh": only true if
  # the tag is exactly what was built, and fetchable.
  [ -z "$(git status --porcelain)" ] \
    || { echo "working tree is dirty -- commit before releasing, or pass --no-release" >&2; exit 1; }
  git fetch -q origin 2>/dev/null || true
  [ -z "$(git log --oneline "origin/$(git branch --show-current)..HEAD" 2>/dev/null)" ] \
    || { echo "HEAD is not pushed -- push before releasing, or pass --no-release" >&2; exit 1; }
  if gh release view "v$VERSION" --repo "$GH_REPO" >/dev/null 2>&1; then
    echo "release v$VERSION already exists on GitHub -- bump the version or pass --no-release" >&2
    exit 1
  fi
  echo "  gh ready, tree clean and pushed, v$VERSION not yet released"
fi
echo "  version $VERSION"

# --------------------------------------------------------------------- build
say "Building Release"
./build.sh Release >/dev/null

AU="$BUILT/AU/$NAME.component"
VST3="$BUILT/VST3/$NAME.vst3"
APP="$BUILT/Standalone/$NAME.app"
for b in "$AU" "$VST3" "$APP"; do
  [ -d "$b" ] || { echo "$b was not produced" >&2; exit 1; }

  # Apple Silicon only (decided 2026-10-15): the engine runs on the Apple GPU.
  archs=$(lipo -archs "$b/Contents/MacOS/$NAME")
  [ "$archs" = "arm64" ] || { echo "$b: expected arm64, got $archs" >&2; exit 1; }

  # Without the precompiled shaders Metal fails silently and every
  # transcription runs on the CPU (see CLAUDE.md, Engine).
  for lib in default.metallib ggml-tensor.metallib; do
    [ -f "$b/Contents/Resources/$lib" ] || { echo "$b: $lib missing" >&2; exit 1; }
  done

  v=$(/usr/libexec/PlistBuddy -c "Print :CFBundleShortVersionString" "$b/Contents/Info.plist")
  [ "$v" = "$VERSION" ] || { echo "$b reports version $v, expected $VERSION" >&2; exit 1; }
  echo "  $(basename "$b")  $archs  $v  shaders present"
done

[ -f "$SF_FILE" ] || { echo "the soundfont is missing: $SF_FILE (build.sh fetches it)" >&2; exit 1; }
[ "$(shasum -a 256 "$SF_FILE" | cut -d' ' -f1)" = "$SF_SHA256" ] \
  || { echo "the soundfont does not match its pinned digest" >&2; exit 1; }
echo "  GeneralUser-GS.sf2  digest ok"

# --------------------------------------------------------------------- sign
# Hardened runtime and a secure timestamp are both required for notarization.
say "Signing"
for b in "$AU" "$VST3"; do
  codesign --force --options runtime --timestamp --sign "$APP_IDENTITY" "$b"
done
codesign --force --options runtime --timestamp \
         --entitlements packaging/standalone.entitlements \
         --sign "$APP_IDENTITY" "$APP"
for b in "$AU" "$VST3" "$APP"; do
  codesign --verify --strict --deep "$b"
  echo "  $(basename "$b") signed and verified"
done
codesign -d --entitlements - --xml "$APP" 2>/dev/null | grep -q "device.audio-input" \
  || { echo "the app lost its microphone entitlement" >&2; exit 1; }
echo "  the app carries the microphone entitlement"

# ------------------------------------------------------------------ package
say "Packaging"
rm -rf "$OUT/components" "$OUT/resources" "$OUT/soundfont-root"
mkdir -p "$OUT/components" "$OUT/resources" "$OUT/soundfont-root"

# What the installer's licence page shows: the plain-words terms, then the
# full licence.
{ cat packaging/terms.txt; echo; echo "--------------------------------------------------------------------------------"; echo; cat LICENSE; } \
  > "$OUT/resources/License.txt"

# Bundles are made non-relocatable: otherwise Installer may "upgrade" a copy
# it finds elsewhere (a build folder, ~/Library/Audio/Plug-Ins) instead of
# installing where this package says.
component() {   # bundle, install location, identifier, output
  local plist="$OUT/components/$3.plist"
  pkgbuild --analyze --root "$(dirname "$1")" "$plist" >/dev/null
  # --analyze lists every bundle in the folder; keep only ours, fixed in place.
  local n i
  n=$(/usr/libexec/PlistBuddy -c "Print" "$plist" | grep -c "RootRelativeBundlePath" || true)
  for ((i = 0; i < n; i++)); do
    /usr/libexec/PlistBuddy -c "Set :$i:BundleIsRelocatable false" "$plist" 2>/dev/null \
      || /usr/libexec/PlistBuddy -c "Add :$i:BundleIsRelocatable bool false" "$plist"
  done
  # --root packs the whole folder: it must hold only this bundle.
  [ "$(ls -A "$(dirname "$1")")" = "$(basename "$1")" ] \
    || { echo "$(dirname "$1") holds more than $(basename "$1")" >&2; exit 1; }
  pkgbuild --root "$(dirname "$1")" --component-plist "$plist" \
           --install-location "$2" --identifier "$3" --version "$VERSION" \
           "$4" >/dev/null
}
component "$AU"   /Library/Audio/Plug-Ins/Components com.AudioFlower.AudioToMIDI.au   "$OUT/components/AU.pkg"
component "$VST3" /Library/Audio/Plug-Ins/VST3       com.AudioFlower.AudioToMIDI.vst3 "$OUT/components/VST3.pkg"
component "$APP"  /Applications                      com.AudioFlower.AudioToMIDI.app  "$OUT/components/App.pkg"

cp "$SF_FILE" "$OUT/soundfont-root/"
pkgbuild --root "$OUT/soundfont-root" \
         --install-location "/Library/Application Support/AudioFlower/soundfonts" \
         --identifier com.AudioFlower.soundfont.GeneralUserGS --version "2.0.3" \
         "$OUT/components/Soundfont.pkg" >/dev/null
echo "  four component packages built"

# hostArchitectures="arm64" alone: Installer refuses an Intel Mac outright.
cat > "$OUT/distribution.xml" <<XML
<?xml version="1.0" encoding="utf-8" standalone="yes"?>
<installer-gui-script minSpecVersion="2">
    <title>Audio to MIDI $VERSION</title>
    <organization>art.audioflower</organization>
    <license file="License.txt"/>
    <!-- rootVolumeOnly: the startup disk only, and no destination page.
         Everything goes in its /Library and /Applications, so another disk
         would be wrong. (A <domains> element restricted the choice too, but
         still showed the page with the other options greyed out.) -->
    <options customize="allow" require-scripts="false" hostArchitectures="arm64" rootVolumeOnly="true"/>
    <volume-check>
        <allowed-os-versions>
            <os-version min="11.0"/>
        </allowed-os-versions>
    </volume-check>
    <choices-outline>
        <line choice="au"/>
        <line choice="vst3"/>
        <line choice="app"/>
        <line choice="sounds"/>
    </choices-outline>
    <choice id="au" title="Audio Unit (AU)" description="Installs to /Library/Audio/Plug-Ins/Components">
        <pkg-ref id="com.AudioFlower.AudioToMIDI.au"/>
    </choice>
    <choice id="vst3" title="VST3" description="Installs to /Library/Audio/Plug-Ins/VST3">
        <pkg-ref id="com.AudioFlower.AudioToMIDI.vst3"/>
    </choice>
    <choice id="app" title="Standalone app" description="Installs Audio to MIDI.app to /Applications">
        <pkg-ref id="com.AudioFlower.AudioToMIDI.app"/>
    </choice>
    <choice id="sounds" title="Preview sounds" description="GeneralUser GS (32 MB), the instruments the preview plays the notes with. Always installed."
            start_selected="true" start_enabled="false">
        <pkg-ref id="com.AudioFlower.soundfont.GeneralUserGS"/>
    </choice>
    <pkg-ref id="com.AudioFlower.AudioToMIDI.au" version="$VERSION">#AU.pkg</pkg-ref>
    <pkg-ref id="com.AudioFlower.AudioToMIDI.vst3" version="$VERSION">#VST3.pkg</pkg-ref>
    <pkg-ref id="com.AudioFlower.AudioToMIDI.app" version="$VERSION">#App.pkg</pkg-ref>
    <pkg-ref id="com.AudioFlower.soundfont.GeneralUserGS" version="2.0.3">#Soundfont.pkg</pkg-ref>
</installer-gui-script>
XML

productbuild --distribution "$OUT/distribution.xml" \
             --package-path "$OUT/components" \
             --resources "$OUT/resources" \
             --sign "$INSTALLER_IDENTITY" \
             "$PKG" >/dev/null
echo "  $PKG signed"

# ----------------------------------------------------------------- notarize
if [ "$SKIP_NOTARIZE" -eq 1 ]; then
  say "Skipping notarization"
  echo "  NOT SHIPPABLE. Without notarizing AND stapling, Gatekeeper warns."
else
  say "Notarizing (a few minutes)"
  xcrun notarytool submit "$PKG" --keychain-profile "$NOTARY_PROFILE" --wait
  # Un-stapled still passes on this machine (cached) and for anyone online;
  # it fails offline. Staple, then verify.
  say "Stapling"
  xcrun stapler staple "$PKG"
fi

# ------------------------------------------------------------------- verify
say "Verifying"
pkgutil --check-signature "$PKG" | sed 's/^/  /' | head -4
# What each part installs where. (Each payload also lists "._" entries:
# macOS's com.apple.provenance tags on the build products, which Installer
# folds back into attributes. EQ's notarized 1.0.0 has them too.)
EXPANDED="$OUT/expanded"
rm -rf "$EXPANDED"
pkgutil --expand "$PKG" "$EXPANDED"
for p in "$EXPANDED"/*.pkg; do
  where=$(grep -oE 'install-location="[^"]*"' "$p/PackageInfo" | cut -d'"' -f2)
  top=$(lsbom -s "$p/Bom" | grep -vE '^\.$' | grep -vE '^\./[^/]+/' | grep -v '/\._')
  echo "  $(basename "$p" .pkg): ${top#./} -> $where"
done
rm -rf "$EXPANDED"
if [ "$SKIP_NOTARIZE" -eq 0 ]; then
  # -vvv is load-bearing: spctl prints nothing on success without it.
  spctl -a -vvv -t install "$PKG" 2>&1 | sed 's/^/  /'
  xcrun stapler validate "$PKG" | sed 's/^/  /'
fi

# ------------------------------------------------------------------ release
if [ "$NO_RELEASE" -eq 0 ]; then
  say "Releasing v$VERSION"
  if git rev-parse "v$VERSION" >/dev/null 2>&1; then
    echo "  tag v$VERSION already exists locally, reusing"
  else
    git tag -a "v$VERSION" -m "Audio to MIDI $VERSION"
    echo "  tagged v$VERSION"
  fi
  git push -q origin "v$VERSION"

  NOTES="$OUT/release-notes.md"
  cat > "$NOTES" <<NOTE
Turns audio into MIDI, one clip per instrument. Audio Unit, VST3 and a standalone app for macOS.

**[Download Audio-to-MIDI-$VERSION.pkg](https://github.com/$GH_REPO/releases/download/v$VERSION/Audio-to-MIDI-$VERSION.pkg)** · $(du -h "$PKG" | cut -f1 | tr -d ' ') · macOS 11 or later · Apple Silicon

Signed and notarized with Apple. The installer offers the AU, the VST3 and the
standalone app as separate choices, and always installs the preview's sounds
(GeneralUser GS). The transcription model (MuScriptor, CC BY-NC 4.0: free for
non-commercial use) downloads inside the plugin the first time you use it.

### Verifying this build

The plugin is AGPLv3, so the source that produced this binary is the source in
this tag:

\`\`\`
git clone --recursive https://github.com/$GH_REPO && cd audio-to-midi
git checkout v$VERSION
./package.sh --no-release
\`\`\`

You will need your own Developer ID certificates, so the signature will differ
-- but everything the signature wraps is built from what is here.
NOTE

  gh release create "v$VERSION" "$PKG" \
     --repo "$GH_REPO" --title "Audio to MIDI $VERSION" --notes-file "$NOTES" >/dev/null
  echo "  published https://github.com/$GH_REPO/releases/tag/v$VERSION"
fi

say "Done"
echo "  $PKG  ($(du -h "$PKG" | cut -f1))"
if [ "$SKIP_NOTARIZE" -eq 1 ]; then
  echo "  Test build only. Re-run without --skip-notarize to produce a release."
fi
