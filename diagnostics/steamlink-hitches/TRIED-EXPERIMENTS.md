# Steam Link freezes: tried experiments

Read this before proposing a previously attempted fix. “Did not solve” means insufficient as a standalone remedy; it does not establish that a setting has no timing effect. Keep user observations separate from instrumented comparisons.

| Recorded | Experiment | Outcome and evidence | Future handling |
|---|---|---|---|
| 2026-09-15 | Host `driver_vrlink.asyncSend=true` | **User explicitly reports already tested; did not solve the freezes.** Exact run dates, APK hashes and synchronized counter traces for that test are unavailable. This supersedes the pending asyncSend recommendation in the paired-archive analysis. | Do not propose enabling it again as a new fix. Repeat only for a specifically justified measurement, acknowledging the prior failure. |
| 2026-09-15 | Decoder input buffering / staging v1 | **Whole-view freezes persisted.** Installed Buffered helper verified on exact 2.0.23/5002363; subsequent trace retained interruptions and UDP receive-buffer errors. See [experiment record](EXPERIMENT-2026-09-15-decoder-staging-v1.md) and [telemetry](TELEMETRY-2026-09-15.md). | Not a proven fix. Use **Observe + pipeline telemetry** for diagnosis without reintroducing staging. |
| 2026-09-15 | Fixed bandwidth 400→150 Mbit/s for 2 minutes | User reported freezes **about the same**. Authorized comparison restored the original setting; prior capture records remain under `build/live-hitch-20260915`. | Do not present the same bandwidth reduction as an untried remedy. It did not isolate packet burst timing. |

## Current experiment

**UDP receive buffer: 1→8 MiB** is the next user-authorized experiment, separate from recommended bundles and default-off. Exact supported bases are 2.0.22/5002322 and 2.0.23/5002363. Its result remains **pending headset testing**; do not record it as successful or failed before that evidence exists.

Rationale and version/host boundaries: [Android XR and PC comparison](XR-HOST-COMPARISON-2026-09-15.md). Direct socket-drop and decoder-gap evidence: [Observe results](OBSERVE-RESULTS-2026-09-15.md).
