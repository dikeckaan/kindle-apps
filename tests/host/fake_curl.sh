#!/bin/sh
# Stand-in for curl in tests. Records its argv (one per line) in $FAKE_CURL_ARGS; FAKE_CURL_MODE picks the behaviour.
# With "-o FILE" the body goes to FILE (as curl does), otherwise to stdout.
[ -n "$FAKE_CURL_ARGS" ] && printf '%s\n' "$@" > "$FAKE_CURL_ARGS"
OUT=
while [ $# -gt 0 ]; do
    if [ "$1" = "-o" ] && [ $# -gt 1 ]; then OUT="$2"; shift; fi
    shift
done
[ -n "$OUT" ] && exec > "$OUT"
case "$FAKE_CURL_MODE" in
    fail) printf 'partial'; echo "curl: (22) The requested URL returned error: 404" >&2; exit 22 ;;
    hang) printf 'partial'; sleep 30 ;;
    big) head -c 3000000 /dev/zero | tr '\0' x ;;
    empty) ;;
    endless) yes xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx ;; # no Content-Length: curl's --max-filesize cannot stop it
    *) printf 'hello body' ;;
esac
