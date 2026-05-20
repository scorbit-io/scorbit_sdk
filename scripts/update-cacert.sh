#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
URL="${SCORBIT_CACERT_URL:-https://curl.se/ca/cacert.pem}"
OUT="$REPO_ROOT/assets/certs/cacert.pem"
TMP="$(mktemp)"

cleanup() {
    rm -f "$TMP"
}
trap cleanup EXIT

if command -v curl >/dev/null 2>&1; then
    curl --fail --location --silent --show-error "$URL" --output "$TMP"
else
    python3 - "$URL" "$TMP" <<'PY'
import sys
import urllib.request

url, out = sys.argv[1], sys.argv[2]
with urllib.request.urlopen(url, timeout=60) as response:
    data = response.read()
with open(out, "wb") as f:
    f.write(data)
PY
fi

mkdir -p "$(dirname "$OUT")"
install -m 0644 "$TMP" "$OUT"

sha256="$(python3 - "$OUT" <<'PY'
import hashlib
import sys

with open(sys.argv[1], "rb") as f:
    print(hashlib.sha256(f.read()).hexdigest())
PY
)"

echo "Updated assets/certs/cacert.pem"
