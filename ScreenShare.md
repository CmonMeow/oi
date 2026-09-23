# Screen sharing

Click **SCREEN**, then **Choose screen or window** in the share window. Windows/WebView2
asks which surface to capture. Once selected, a green `name screen [watch]` link
appears in chat. Clicking it opens the viewer. **STOP SHARE**, the capture indicator's
stop action, closing the share window, or disconnecting ends sharing. Old links
show `[ended]`. Joining users receive the currently active offers.

This is video only. Existing voice chat continues separately. There is no remote
input/control, automatic screen selection, or automatic viewing.

## Media and connections

- H.264 is required for sending and receiving; there is no silent AV1/VP8 fallback.
- Capture targets 60 fps, limited to 1920 x 1080 at 60 fps, with up to 2 Mbps per viewer.
  Actual size/rate adapts to the selected surface, encoder, and connection.
- WebView2 supplies the screen picker, WebRTC H.264 encoder/decoder, ICE, and DTLS-SRTP.
  Hardware encoding depends on the runtime and hardware; software encoding is possible.
- Each viewer has an endpoint-encrypted WebRTC connection carried through the room
  server. The page bounds concurrent connections to 25. Each viewer adds a separate
  stream: both the sharer's upload and the server's relay traffic grow with viewers.
- Native UDP proxies bind only to 127.0.0.1. The browser receives only its local
  proxy's candidate; real ICE candidates are never exchanged. SDP address fields
  are sanitized in both directions. No STUN service or direct fallback is used.
- Opaque ICE/DTLS/SRTP datagrams travel over the existing authenticated room
  connection, without another listening port or relay installation. Video packets
  are unreliable so losses do not hold up newer frames; WebRTC handles recovery.
- Relayed traffic is included in the main title's incoming/outgoing Mbps totals.
- The screen window title shows each active video's resolution and measured
  displayed frame rate, updated once per second.

## Privacy and bounds

Other clients do not receive the sharer/viewer IP addresses. The room host still
sees connected clients' addresses. The browser applies DTLS-SRTP encryption
between endpoints; a passive relay cannot read the video. The room's authenticated
server assigns sender identities and permits setup only after the viewer requests
an active share. This trusts the room server to route SDP/fingerprints honestly;
it is not independent identity verification against a malicious server.

Offers use random 128-bit IDs, and each viewing connection gets a fresh 128-bit ID.
Stopping, restarting, leaving, or disconnecting invalidates previous connections.
Wire messages, datagram queues, event queues, participants, and setup rates are
bounded. A peer cannot stop or signal for another peer's share. Media parsing and
decoding run in the installed, updateable WebView2 browser runtime.
Rejected share/watch requests reset the pending operation and report an error so
the user can retry. Screen windows and asynchronous WebView initialization use a
consistent DPI context, including after closing and reopening the window.

The page is an embedded resource served at an internal `https://oi/` origin. External
navigation, popups, camera/microphone permission requests, and page resource
requests are blocked. Network input is delivered as structured strings, never
executed as JavaScript or inserted as HTML. WebView2 stores its local profile in
`%LOCALAPPDATA%\oi\ScreenShare`.

## Compatibility and validation

Release x64. Protocol version 28: update the host and clients together. An installed
Microsoft Edge WebView2 Runtime with H.264 capabilities is required only for screen
sharing; unsupported/missing runtimes produce a clear error.

Automated generated-video tests verify H.264 decoding, connected DTLS, loopback-only
remote candidates, and three close/reopen cycles through a separate room host.
Tests do not capture the user's desktop. Synthetic relay load additionally checks
room transport alongside voice/files; it does not establish full-HD encoder capacity.
WAN throughput, real screen-picker interaction, and performance on other machines
still need real-world testing.

References:
- https://learn.microsoft.com/microsoft-edge/webview2/
- https://learn.microsoft.com/microsoft-edge/webview2/concepts/overview-features-apis
- https://webrtc.org/getting-started/peer-connections
