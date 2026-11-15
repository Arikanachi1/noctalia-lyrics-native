# Spectrum responsiveness

User requests: 64 symmetric downward bars and faster appearance/reaction.

Evidence: old companion checked its lease every 250ms, output every 40ms,
UI delayed paints until 32ms, envelope attack/release were 35/160ms. Review
also found the old five-second retry deadline survived an intentional close,
delaying a quick reopen even after a successful prior capture.

- [x] Add failing FFT response, UI startup ordering/count/paint timing tests.
- [x] Compute 32 independent frequencies, mirror to64 bars (4px +2px gap).
- [x] Use lease file notifications, publish initial lease before process spawn.
- [x] Stream at50Hz, paint next display frame; attack12ms/release60ms.
- [x] Reproduce heartbeat truncation stopping capture in real companion test;
      preserve previous bounded lease during transient empty writes.
- [x] Extract real retry policy, reproduce quick reopen failure with pure unit
      test and reset backoff on intentional stop; preserve outage backoff.
- [x] Add private-socket reconnection probe, explicitly skip when bind denied.
- [x] Build, CTest and plugin lint; independent review finding fixed above.
- [x] Install and verify deployed files.

Latest restricted CTest: 10 passed, two pre-existing Unix socket/D-Bus tests
blocked by environment, private-socket restart probe skipped. FFT/retry policy,
real companion lease notifications and UI regressions passed. Live audio
latency has not been measured in this environment.
Version1.7.2 installed with runtime/config backups and successful plugin reload
responses. Deployed companion SHA-256 and Lua/manifest bytes match the build
and project. Real no-server lease wake-up measured21.7ms; this is scheduling
latency, not an end-to-end live audio latency measurement.
