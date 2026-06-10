#!/bin/sh
# Regression suite for the GW-BASIC interpreter.
#   tests/X.bas   -> ../basic -r X.bas   (stdin from X.in when present)
#   tests/X.repl  -> piped into ../basic (immediate mode)
# Output is compared against the golden X.out.
cd "$(dirname "$0")"
BASIC=${BASIC:-../basic}
pass=0; fail=0
for t in *.bas *.repl; do
    [ -e "$t" ] || continue
    name=${t%.*}
    out=$name.out
    [ -f "$out" ] || continue
    case "$t" in
        *.repl) actual=$(timeout 15 "$BASIC" < "$t" 2>&1) ;;
        *) if [ -f "$name.in" ]; then
               actual=$(timeout 15 "$BASIC" -r "$t" < "$name.in" 2>&1)
           else
               actual=$(timeout 15 "$BASIC" -r "$t" < /dev/null 2>&1)
           fi ;;
    esac
    if [ "$actual" = "$(cat "$out")" ]; then
        pass=$((pass+1))
    else
        fail=$((fail+1))
        echo "FAIL: $name"
        echo "$actual" | diff "$out" - | head -8
    fi
    rm -f ./*.dat names.txt
done
echo "passed $pass, failed $fail"
[ "$fail" -eq 0 ]
