# Screen sharing

Click **SCREEN**, then **Choose screen or window** in the share window. Windows/WebView2
asks which surface to capture. Once selected, a green `name screen [watch]` link
appears in chat. Clicking it opens the viewer. **STOP SHARE**, the capture indicator's
stop action, closing the share window, or disconnecting ends sharing. Old links
show `[ended]`. Joining users receive the currently active offers.

This is video only. Existing voice chat continues separately. There is no remote
input/control, automatic screen selection, or automatic viewing.

## Media and connections

- AV1 is required for sending and receiving; there is no silent H.264/VP8 fallback.
- Capture targets 60 fps, limited to 1920 x 1080 at 60 fps, with up to 2 Mbps per viewer.
  Actual size/rate adapts to the selected surface, encoder, and connection.
- WebView2 supplies the screen picker, WebRTC AV1 encoder/decoder, ICE, and DTLS-SRTP.
  Hardware encoding depends on the runtime and hardware; software encoding is possible.
- Each viewer connects directly to the sharer. Uplink and encoding cost increase
  with viewers. The page bounds concurrent peer connections to 25.
- The chat server carries only share announcements, SDP, and ICE setup. Video is
  never sent through the chat transport. No TURN/video relay is configured.
- `stun:stun.l.google.com:19302` discovers public candidates. STUN receives address
  discovery traffic, not screen content. Sharer and viewer can learn each other's
  IP addresses. Restrictive NAT/firewalls can prevent a direct connection; the
  viewer reports failure or a 30-second setup timeout instead of relaying video.
- Direct traffic is included in the main title's incoming/outgoing Mbps totals.
- The screen window title shows each active video's resolution and measured
  displayed frame rate, updated once per second.

## Privacy and bounds

The browser applies DTLS-SRTP encryption between endpoints. The room's authenticated
server assigns sender identities and permits setup only after the viewer requests
an active share. This trusts the room server to route SDP/fingerprints honestly;
it is not independent identity verification against a malicious server.

Offers use random 128-bit IDs, and each viewing connection gets a fresh 128-bit ID.
Stopping, restarting, leaving, or disconnecting invalidates previous connections.
Wire messages, candidate queues, event queues, participants, and setup rates are
bounded. A peer cannot stop or signal for another peer's share. Media parsing and
decoding run in the installed, updateable WebView2 browser runtime.
Rejected share/watch requests reset the pending operation and report an error so
the user can retry. Screen windows and asynchronous WebView initialization use a
consistent DPI context, including after closing and reopening the window.

The page is an embedded resource served at a synthetic HTTPS origin. External
navigation, popups, camera/microphone permission requests, and page resource
requests are blocked. Network input is delivered as structured strings, never
executed as JavaScript or inserted as HTML. WebView2 stores its local profile in
`%LOCALAPPDATA%\oi\ScreenShare`.

## Compatibility and validation

Release x64. Protocol version 27: update the host and clients together. An installed
Microsoft Edge WebView2 Runtime with AV1 capabilities is required only for screen
sharing; unsupported/missing runtimes produce a clear error.

Automated tests under `build/screenshare` exercise bounded signaling and generated
video, rather than capturing the user's desktop. They check negotiated AV1,
decoded frames, direct (non-relay) ICE candidates, connected DTLS, and ending offers.
Actual screen-picker selection and remote-network NAT combinations still require
interactive testing on the relevant PCs. The Windows screenshot helper could not
capture this machine's UI (`SetIsBorderRequired: No such interface supported`).

References:
- https://learn.microsoft.com/microsoft-edge/webview2/
- https://learn.microsoft.com/microsoft-edge/webview2/concepts/overview-features-apis
- https://webrtc.org/getting-started/peer-connections
