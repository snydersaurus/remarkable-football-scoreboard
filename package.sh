#!/usr/bin/env bash
# Build the release zips: one per league, each self-contained.
#
#   ./build.sh && ./package.sh
#
# What someone downloads should not need this repo, Docker, or the 460MB
# reMarkable SDK -- so each zip carries the built bundle and an install.sh that
# only needs ssh and scp.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
DIST="$HERE/dist"
IMAGE="${IMAGE:-rmpp-sdk-5.7}"

for L in cfb nfl; do
    [ -f "$HERE/appload-native/out/$L/backend/entry" ] || {
        echo "nothing built for $L — run ./build.sh first"; exit 1; }
done

echo "==> stripping the binaries"
# 2.4MB of debug symbols nobody downloading this can use.
docker run --rm --platform linux/amd64 -v "$HERE":/src "$IMAGE" bash -lc '
  source /opt/rmpp-sdk/environment-setup-*-remarkable-linux
  for L in cfb nfl; do
      f=/src/appload-native/out/$L/backend/entry
      case "$(file -b "$f")" in *"not stripped"*) $STRIP "$f";; esac
  done
'

rm -rf "$DIST"; mkdir -p "$DIST"

for L in cfb nfl; do
    APPID="$(grep -o '"id"[^,]*' "$HERE/appload-native/out/$L/manifest.json" \
             | head -1 | sed 's/.*: *"//;s/"//')"
    NAME="$(grep -o '"name"[^,]*' "$HERE/appload-native/out/$L/manifest.json" \
             | head -1 | sed 's/.*: *"//;s/"//')"
    STAGE="$DIST/$APPID"

    echo "==> packaging $APPID ($NAME)"
    mkdir -p "$STAGE"
    cp -R "$HERE/appload-native/out/$L/." "$STAGE/"

    # A standalone installer: no repo, no SDK, just ssh and scp.
    cat > "$STAGE/install.sh" <<INSTALLER
#!/usr/bin/env bash
# Install $NAME on a reMarkable running xovi + AppLoad.
#
#   RM_HOST=10.11.99.1 ./install.sh
#
# Needs developer mode and SSH. The password is on the tablet:
# Settings > About > Copyright and licenses.
set -euo pipefail

RM_HOST="\${RM_HOST:-10.11.99.1}"
HERE="\$(cd "\$(dirname "\$0")" && pwd)"
DEST="/home/root/xovi/exthome/appload/$APPID"

[ -f "\$HERE/backend/entry" ] || { echo "run this from inside the unzipped folder"; exit 1; }

echo "==> installing to \$DEST on \$RM_HOST"
ssh "root@\${RM_HOST}" "mkdir -p \${DEST}/backend"

# Copy to a temp name and move it into place. AppLoad leaves a backend running
# after the app closes, and scp onto a live binary fails with a bare
# "dest open ... Failure"; a move only relinks the directory entry.
scp "\$HERE/backend/entry" "root@\${RM_HOST}:\${DEST}/backend/entry.new"
scp "\$HERE/backend/league" "root@\${RM_HOST}:\${DEST}/backend/league.new"
ssh "root@\${RM_HOST}" "chmod +x \${DEST}/backend/entry.new \\
    && mv -f \${DEST}/backend/entry.new \${DEST}/backend/entry \\
    && mv -f \${DEST}/backend/league.new \${DEST}/backend/league"

# Will the tablet's dynamic linker accept it? Run it against a socket that does
# not exist: it should reach its own connect failure. A loader error instead
# means this build does not match your OS version, and through AppLoad that
# looks like an app that simply never appears.
LOADED="\$(ssh "root@\${RM_HOST}" "\${DEST}/backend/entry /tmp/appload-probe-none.sock 2>&1" || true)"
case "\$LOADED" in
    *"not found"*|*"cannot open shared object"*)
        echo
        echo "\$LOADED"
        echo "This build does not match your tablet's Qt:"
        ssh "root@\${RM_HOST}" 'ls -l /lib/libQt6Core.so.6'
        echo "It is built for OS 5.7.x (Qt 6.8.2). Build from source for other versions."
        exit 1 ;;
esac

# Only ship the neutral icon if no team has been picked -- otherwise this would
# undo the icon the app set when you chose one.
if ssh "root@\${RM_HOST}" "[ -f /home/root/.config/scoreboard/\$(cat "\$HERE/backend/league").json ]"; then
    echo "  keeping your current icon (a team is already chosen)"
    FILES="manifest.json resources.rcc"
else
    FILES="manifest.json resources.rcc icon.png"
fi
for f in \$FILES; do
    scp "\$HERE/\$f" "root@\${RM_HOST}:\${DEST}/\$f.new"
    ssh "root@\${RM_HOST}" "mv -f \${DEST}/\$f.new \${DEST}/\$f"
done

cat <<MSG

Installed $NAME.

AppLoad only reads manifests and icons when xochitl starts, so finish with:

  ssh root@\${RM_HOST} systemctl restart xochitl

That takes about a minute and leaves you on the lock screen. Then open the
AppLoad sidebar and tap $NAME.
MSG
INSTALLER
    chmod +x "$STAGE/install.sh"

    (cd "$DIST" && zip -qr "$APPID.zip" "$APPID")
    rm -rf "$STAGE"
    echo "    dist/$APPID.zip  $(du -h "$DIST/$APPID.zip" | cut -f1)"
done

echo
echo "==> done. Attach both to a release:"
echo "    gh release create vX.Y.Z dist/*.zip --repo <owner>/<repo>"
