# Host browser and directory

The client now opens a clickable host list. `/host` and dedicated startup automatically publish the host using its local username. `/hosts` or the offline HOSTS button reopens the list; CHAT returns to the usual chat view. Direct `/connect` remains available if the directory is offline.

## Run the directory

Run `bin/oi-directory.exe` on the machine reached by the current default host address. Allow and forward **UDP 778** to that machine. The directory can run alongside an oi host on UDP 777. It stays running until its console is closed or Ctrl+C is pressed. No firewall or router settings were changed by this implementation.

The directory identity has already been initialized on this PC at:

`C:\Users\CmonMeow\AppData\Local\oi-directory\identity.key`

The private file is restricted to its owner and SYSTEM. Back it up securely; it is needed to serve listings accepted by this client release. When moving the directory to another machine, securely move this same key and use `oi-directory.exe --key "path\identity.key"`. Do not distribute the key with the client or upload it with the source. This key is separate from the executable code-signing certificate.

`--init` creates an identity only if none exists and prints its public key. Generating a different identity requires updating the public key in `HostDirectoryProtocol.h` and rebuilding the clients and directory. Normal startup rejects a key that does not match this release.

Optional server arguments: `--bind IPv4`, `--port 778`, `--key path`. Both executable projects build with the solution's **Release x64** configuration and use the existing private signing step.

Clients use the current `DEFAULT_NETWORK_ADDRESS` on UDP 778. For a local test or a changed directory address, an optional `DirectoryAddress.txt` beside `oi.exe` can contain a hostname or `IPv4:port`. An alternate directory must use the same trusted identity.

## Hosting behavior

- A host must be reachable from the directory on its game UDP port (normally 777). Each host still needs its own inbound forwarding/firewall setup. The directory does not provide NAT traversal or relay chat, voice, files, or video.
- The directory lists the observed host IP, game port, host name, user count, and protocol version. Hosting therefore publishes the host's address. Ordinary participants' addresses and drive serials are not included.
- Listings refresh every 20 seconds and expire after 90 seconds without renewal. Normal disconnect removes the listing immediately; a crash or lost removal packet falls back to expiry.
- Browsers refresh every 15 seconds, show five hosts per page, and discard stale cached results after 90 seconds. The directory holds up to 256 hosts and only returns compatible protocol versions.
- Listing failure does not stop hosting or direct connections. The client reports whether publication succeeded.

## Validation and limits

Directory responses have Ed25519 signatures verified against a public key compiled into the client. A source-bound cookie precedes listing retrieval and registration. Publication additionally requires a signed challenge answered from the actual game socket with the current registration nonce. Packet sizes, work per iteration, source rates, pending probes, and listing counts are bounded.

Signatures authenticate the directory response; they do not certify a host's identity or content. Names and user counts are supplied by hosts, and the publicly readable list is not a private directory. Requests/listings are not encrypted. The directory holds listings only in memory and does not collect serials or chat content.

Release tests cover actual game-socket registration, connecting to a discovered address through the production connection path and completing the encrypted handshake, pagination, signed replies, rejected cookie replay and altered signatures, rejection of a false claim on another game's port, delayed-proof retries, cancelled registrations, heartbeat renewal, stale expiry, removal, and a nonblocking directory outage. Tests use loopback; external reachability still depends on deployment and port forwarding. Results are in `build/host-directory/run/full-test.log` and `build/host-directory/run/test.log`; the test source is `tests/host-directory/DirectoryTest.cpp`.

The desktop helper could not activate the new client window (`failed to activate captured window`), so visual layout and clicking a row through the actual UI remain unverified. The host browser calls the existing `connectTo` path with the discovered IPv4 and port.

The previous overnight-memory instance remains running as PID 12520. Its old executable was renamed to `bin/oi-idle-test.exe`; `bin/oi.exe` is the new build. See `OvernightIdleMemory.md` before the next memory comparison.
