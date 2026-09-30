#!/usr/bin/env bash
# End-to-end test: build a synthetic ROM with a known soundbank, run n64sfxdump,
# and verify the decoded WAV samples match hand-computed expected values.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
bin="$root/build/n64sfxdump"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

if [[ ! -x "$bin" ]]; then
  echo "building n64sfxdump..."
  cmake -S "$root" -B "$root/build" -DCMAKE_BUILD_TYPE=Release >/dev/null
  cmake --build "$root/build" >/dev/null
fi

python3 "$here/make_fixture.py" "$work/fixture.z64" "$work/expected.txt"

# z64 plus byte-swapped variants
python3 - "$work" <<'PY'
import sys
sp = sys.argv[1]
d = bytearray(open(sp+"/fixture.z64","rb").read())
v = bytearray(d)
for i in range(0, len(v)&~1, 2): v[i],v[i+1]=v[i+1],v[i]
open(sp+"/fixture.v64","wb").write(v)
n = bytearray(d)
for i in range(0, len(n)&~3, 4): n[i],n[i+1],n[i+2],n[i+3]=n[i+3],n[i+2],n[i+1],n[i]
open(sp+"/fixture.n64","wb").write(n)
PY

rc=0
for ext in z64 v64 n64; do
  echo "=== $ext ==="
  rm -rf "$work/out_$ext"
  # Default path: scan for the bank, auto-discover its .tbl, decode.
  "$bin" "$work/fixture.$ext" "$work/out_$ext" >/dev/null
  if python3 "$here/verify.py" "$work/out_$ext" "$work/expected.txt"; then
    echo "  $ext OK"
  else
    echo "  $ext FAILED"; rc=1
  fi
done

echo
if [[ $rc -eq 0 ]]; then echo "ALL TESTS PASSED"; else echo "TESTS FAILED"; fi
exit $rc
