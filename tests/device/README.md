# Device tests

Run on the Kindle over SSH (they pause and restore the Kindle UI):

    cat tests/device/<test>.sh | ssh root@<kindle> 'cat > /tmp/t.sh; setsid sh /tmp/t.sh <args> &'

- `test_statusbar_restored.sh <kui-run.sh>` — status bar stopped before launch is running after exit (result in /tmp/sb.result)
- `test_wedged_app.sh <kui-run.sh>` — an app ignoring SIGTERM is killed within ~6 s and the UI returns
- `test_clock_restore.sh <kui-run.sh> <clock>` — a crashed clock still gets preventScreenSaver restored (result in /tmp/clock.result)
- `test_lipc_helper_death.sh <app-binary>` — killing lipc-wait-event must not make the app spin (≈0 CPU ticks)
