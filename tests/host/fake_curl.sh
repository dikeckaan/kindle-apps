#!/bin/sh
# Stand-in for curl in tests. Records its argv (one per line) in $FAKE_CURL_ARGS; FAKE_CURL_MODE picks the behaviour.
[ -n "$FAKE_CURL_ARGS" ] && printf '%s\n' "$@" > "$FAKE_CURL_ARGS"
case "$FAKE_CURL_MODE" in
    fail) echo "curl: (22) The requested URL returned error: 404" >&2; exit 22 ;;
    hang) sleep 30 ;;
    big) head -c 3000000 /dev/zero | tr '\0' x ;;
    *) printf 'hello body' ;;
esac
