#!/usr/bin/env bash
#
# Create a TAK identity for this firmware and wire it in.
#
#   ./tools/new-device.sh <cert-name> [callsign]
#
#   cert-name   Identity the server authenticates. Becomes the certificate CN
#               and the username in UserAuthenticationFile.xml. Must be unique
#               across the whole server and contain no spaces.
#   callsign    What other operators see on the map. Optional; defaults to the
#               cert-name. These are deliberately separate: the certificate
#               proves who you are, the callsign is only a label.
#
# What it does:
#   1. generates the client certificate on the server (or reuses an existing one)
#   2. registers it as a standard, non-admin user
#   3. downloads a ready-to-compile include/certs.h
#   4. sets TAK_CALLSIGN in include/secrets.h
#
# Existing certs.h is backed up first. Nothing is overwritten silently.

set -euo pipefail

SERVER=root@212.43.155.64
KEY=~/.ssh/id_ed25519_zomro
PROJECT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

die() { echo "error: $*" >&2; exit 1; }

NAME="${1:-}"
CALLSIGN="${2:-${NAME}}"

[ -n "$NAME" ] || die "usage: $0 <cert-name> [callsign]

  $0 esp32-user4                  cert 'esp32-user4', callsign 'esp32-user4'
  $0 esp32-user4 SCOUT-1          cert 'esp32-user4', callsign 'SCOUT-1'"

# The cert name lands in a certificate CN, a filename and an XML attribute.
case "$NAME" in
  *[!A-Za-z0-9_-]*) die "cert-name '$NAME' has invalid characters (use letters, digits, - and _)" ;;
esac
# The callsign is written into XML attributes by the firmware.
case "$CALLSIGN" in
  *[\<\>\&\"]*) die "callsign '$CALLSIGN' contains characters that would break the CoT XML" ;;
esac

SSH="ssh -i $KEY -o BatchMode=yes -o ConnectTimeout=15 $SERVER"

echo "==> checking server access"
$SSH true 2>/dev/null || die "cannot reach $SERVER with $KEY
  If this hangs or is refused, ufw rate-limits port 22 after ~6 connections
  in 30 seconds. Wait a minute and retry."

echo "==> creating identity '$NAME' on the server"
$SSH "/opt/tak/add-device.sh '$NAME' esp32" || die "add-device.sh failed on the server"

REMOTE_FILE="/opt/tak/devices/${NAME}-certs.h"
$SSH "test -s '$REMOTE_FILE'" || die "expected $REMOTE_FILE was not produced"

if [ -f "$PROJECT/include/certs.h" ]; then
  BACKUP="$PROJECT/include/certs.h.bak"
  cp "$PROJECT/include/certs.h" "$BACKUP"
  echo "==> existing certs.h backed up to include/certs.h.bak"
fi

echo "==> downloading certs.h"
scp -q -i "$KEY" -o BatchMode=yes "$SERVER:$REMOTE_FILE" "$PROJECT/include/certs.h"

# A truncated or partial download produces a file that compiles but never
# connects, so verify the three PEM blocks are actually present.
BLOCKS=$(grep -c -- "-----BEGIN" "$PROJECT/include/certs.h" || true)
[ "$BLOCKS" -eq 3 ] || die "certs.h has $BLOCKS PEM blocks, expected 3 (CA, client cert, key)"
grep -q -- "-----BEGIN PRIVATE KEY-----" "$PROJECT/include/certs.h" \
  || die "private key is not in decrypted form - mbedTLS cannot read an encrypted key"

echo "==> setting callsign to '$CALLSIGN'"
SECRETS="$PROJECT/include/secrets.h"
if [ -f "$SECRETS" ]; then
  sed -i "s|^#define TAK_CALLSIGN .*|#define TAK_CALLSIGN \"$CALLSIGN\"|" "$SECRETS"
  grep -E "^#define TAK_CALLSIGN" "$SECRETS" | sed 's/^/    /'
else
  echo "    warning: include/secrets.h not found - copy secrets.example.h and set TAK_CALLSIGN manually"
fi

cat <<EOF

Done.
  identity  $NAME        (certificate CN, how the server authenticates you)
  callsign  $CALLSIGN    (what shows on the map)
  certs.h   include/certs.h

To use TLS on 8089 with this identity:
  - uncomment  #define TAK_USE_TLS 1   in include/config.h
  - set        #define TAK_PORT 8089   in include/secrets.h
  - pio run -e takclient -t upload -t monitor
EOF
