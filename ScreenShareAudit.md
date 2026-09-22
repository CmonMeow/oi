# Screen-sharing audit — September 22, 2026

Scope: native WebView lifetime and input integration, embedded page, screen-signaling routing and limits, and regression testing with the current voice/file transport.

## Corrections

- Changed the embedded origin to `https://oi/`, removing `.invalid` from the browser's sharing label. The executable serves the page; this does not add a website, HTTP listener, or video relay. Failure to install the local resource interceptor now prevents navigation.
- Scaled the host's per-client signaling budget with that client's active screen connections, capped at 25 links. The previous fixed 128-message/128-KiB budget could silently discard valid simultaneous offers and ICE candidates from one sharer. Unconnected clients retain the smaller allowance, and start/watch requests retain their separate rate limit.
- Blocked page reload after initialization. Reload could otherwise destroy media while leaving the native share offer active.
- Made stop/close processing independent of queued asynchronous negotiation. Cancelled queued work cannot reopen a closed connection, and pending browser work is bounded at 256 operations.
- Ignored late connection/track callbacks after removal and rejected duplicate SDP negotiation. An old capture track's end event cannot stop a newer capture.
- Cleaned up capture if initialization fails after acquisition, and prevented overlapping traffic-statistics queries from moving byte counters backward and double-counting traffic.

## Five-minute room test

25 client processes plus one host, using loopback-only transport and the current production runtime. Four phases: two speakers, all 25 speaking, two speakers plus file transfers, then file transfers without voice. Files use the application's encrypted transfer path and are verified byte-for-byte.

Three screen-setup rounds ran during the workload. Each round routes a maximum-sized 24,000-byte synthetic offer and answer for each of 25 viewers, plus five 1,000-byte synthetic ICE payloads per viewer. These test the real authenticated/encrypted room signaling path, not WebRTC parsing.

| Check | Result |
|---|---:|
| Unexpected disconnects | 0 |
| Voice deliveries | 2,325,000 / 2,325,000 |
| Verified files | 50 / 50 |
| Screen offers / answers | 75 / 75 each |
| Screen candidate payloads | 375 / 375 |
| Host peak sampled working set | 62.0 MB |
| Host final sampled working set | 61.5 MB |
| Host upload during all-speaker phase | 28.64 Mbps |
| Worst client p99 scheduled-frame delay | 134 ms |

All 26 processes exited normally. No failure/timeout/error notices appeared in their logs. Scheduled-frame delay includes test scheduling and processing; it is not the title-bar ping or measured listening latency.

Detailed artifacts: `build/x64/load25/screen-run-a1e6be0bc276442cb37230fae02bdea3/summary.json` and per-process results.

## Targeted regressions

- Signaling: authenticated routing, spoof rejection, bounds, malformed input, share replacement, departure, host sharing/watching, late joins, and explicit rate-limit rejection.
- Embedded JavaScript: immediate stop during blocked negotiation, cancelled queued watch, stale track callback, duplicate SDP, and bounded pending work.
- Native WebView: the `https://oi` origin, blocked reload preserving page state, mouse-capture cancellation and focus-loss cleanup. Foreground focus round trips depend on Windows allowing the test to activate its windows and are not used as an automated pass criterion here.

## Separate AV1 fan-out test

Passed one sharer with 24 receiving WebRTC connections for at least 60 seconds
after all receivers began decoding. Every fresh statistics sample showed all 24
receiving AV1 over connected DTLS with direct, non-relay ICE candidates. Decoded
frames kept advancing; the slowest receiver had decoded 2,740 frames at completion.
No application errors occurred. Results: `build/screenshare/fanout-result.txt`.

The source was generated 640 x 360 video targeting 60 fps. To fit this 8-GB PC,
the test grouped 24 receiver connections in one browser window and used a second
window for the production sender. This validates concurrent media connections,
not 24 independent UI instances or sustained 60 fps at every receiver. It ran
separately from the 25-process voice/file test. An initial duration test read cached
statistics; the final harness waits for fresh asynchronous results before checking
progress.

Release x64 build and `git diff --check` passed. The rebuilt executable is
`bin/oi.exe`; no wire-format/protocol-version change was needed.

## Limits

These are local tests, not a WAN capacity guarantee or a proof that arbitrary video/decoder inputs are safe. Physical microphone/playback, the real screen picker/banner, NAT combinations, and 25 independent PCs displaying full-HD 60-fps video still require testing on those systems. The room host remains trusted for signaling; this audit does not add independent peer identity verification against a malicious host.

Test sources and reports remain in ignored `build` directories. Test executables and generated transfer payloads are removed after testing; normal output remains `bin/oi.exe`.
