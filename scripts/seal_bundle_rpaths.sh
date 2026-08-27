#!/usr/bin/env bash
#
# Strip absolute LC_RPATH entries from every Mach-O in a deployed .app, then
# re-sign what changed.
#
# Why this is needed. macdeployqt rewrites dependency references into the bundle
# (@executable_path / @loader_path / @rpath) but leaves behind the absolute rpath
# the linker baked in for the build-machine Qt -- e.g. /opt/homebrew/lib for a
# Homebrew Qt. Several Qt frameworks still refer to their siblings as plain
# @rpath/QtCore.framework/..., and dyld searches the *main executable's* rpaths
# when resolving those. So the leftover absolute rpath is a live route back out
# of the bundle: dyld finds /opt/homebrew/lib/QtCore.framework (a symlink into
# the Cellar) and loads a SECOND copy of QtCore alongside the bundled one:
#
#   objc[…]: Class QT_ROOT_LEVEL_POOL__… is implemented in both
#     …/TrainsOnMap.app/Contents/Frameworks/QtCore.framework/…/QtCore and
#     /opt/homebrew/Cellar/qtbase/6.11.1/lib/QtCore.framework/…/QtCore.
#     This may cause spurious casting failures and mysterious crashes.
#
# On this machine that hit QtCore and QtGui. It only reproduces where the build
# machine's Qt is still present, which makes it exactly the kind of bug that
# survives local testing and fails on someone else's Mac.
#
# This must run AFTER macdeployqt: macdeployqt resolves @rpath dependencies via
# those same rpaths while it decides what to copy, so removing them earlier
# leaves frameworks out of the bundle. Removing them afterwards is safe because
# every dependency reference is @-relative by then -- the absolute rpath is dead
# weight that only dyld's fallback search can still reach.
#
# install_name_tool invalidates code signatures, so each modified binary is
# re-signed ad-hoc (matching what macdeployqt itself applies), then the bundle
# seal is refreshed. A Developer ID build should re-sign with the real identity
# after this instead.
#
# Usage: seal_bundle_rpaths.sh /path/to/Foo.app

set -euo pipefail

APP="${1:?usage: seal_bundle_rpaths.sh /path/to/Foo.app}"
[ -d "$APP" ] || { echo "seal_bundle_rpaths: no such bundle: $APP" >&2; exit 1; }

modified=()

while IFS= read -r f; do
    # Only Mach-O images have rpaths. Note framework binaries are typically not
    # marked executable, so this deliberately does not filter on the +x bit.
    file "$f" | grep -q "Mach-O" || continue

    # Absolute rpaths only: @executable_path / @loader_path stay, they are how
    # the bundle finds its own frameworks.
    external=$(otool -l "$f" 2>/dev/null \
        | awk '/LC_RPATH/ {r=1} r && /path /{print $2; r=0}' \
        | grep -v '^@' || true)
    [ -n "$external" ] || continue

    while IFS= read -r rp; do
        [ -n "$rp" ] || continue
        install_name_tool -delete_rpath "$rp" "$f" 2>/dev/null \
            && echo "  removed rpath $rp from ${f#"$APP"/}" \
            || echo "  WARNING: could not remove rpath $rp from ${f#"$APP"/}" >&2
    done <<< "$external"

    modified+=("$f")
done < <(find "$APP" -type f)

if [ ${#modified[@]} -eq 0 ]; then
    echo "seal_bundle_rpaths: no absolute rpaths found; bundle already sealed"
    exit 0
fi

# Re-sign each rewritten binary, then reseal the bundle itself.
for f in "${modified[@]}"; do
    codesign --force --sign - "$f" >/dev/null 2>&1 \
        || echo "  WARNING: ad-hoc re-sign failed for ${f#"$APP"/}" >&2
done
codesign --force --sign - "$APP" >/dev/null 2>&1 \
    || echo "  WARNING: ad-hoc re-sign failed for the bundle" >&2

echo "seal_bundle_rpaths: sealed ${#modified[@]} binaries in ${APP##*/}"
