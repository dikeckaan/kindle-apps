#!/bin/sh
# fake lipc-get-prop / lipc-set-prop: properties live in $FAKE_LIPC_DB/<publisher>.<prop>
case "$(basename "$0")" in
    lipc-get-prop) cat "$FAKE_LIPC_DB/$1.$2" 2>/dev/null || exit 1 ;;
    lipc-set-prop) printf '%s\n' "$3" > "$FAKE_LIPC_DB/$1.$2" ;;
esac
