# File transfers

Drop up to eight local files into chat. Each file is offered to the people currently in the room, including the host. The sender sees a red offer; recipients see a red filename/size link. Click to choose a new local destination. Click an active download to cancel, or your own offer to withdraw it. Files never open automatically. No image previews or external URL fetching are added.

## Limits and lifetime

- 4 GiB (4,294,967,296 bytes) per file; eight outstanding source files and eight offers per sender (32 incoming offers total).
- Four active uploads and four active downloads per client.
- Unaccepted offers expire after ten minutes. Accepted transfers have no total duration limit and continue past offer expiry; they time out only after 30 seconds without progress. Sender must remain connected.
- 8 KiB chunks, one outstanding chunk per recipient, 128 KiB/s aggregate upload cap. Files use normal-priority reliable packets; voice keeps its existing path.
- Relay traffic is rate-limited per sender and globally. File traffic stops entering a backed-up transport queue.
- Sources are held read-only, denying concurrent writers. Destinations use random partial filenames, verify length and a BLAKE2b digest, and rename only after completion, without replacing existing files. Cancellation, errors and disconnects remove partial files. Abrupt process termination can leave a `.oi-*.part` file in the selected folder.
- On filesystems supporting alternate data streams, downloaded files receive Windows Internet-zone provenance. Files are not scanned, decoded, extracted, or executed.

## Privacy and trust

Client-to-client packets are forwarded through the host; no peer addresses are included. File offers, names and contents use libsodium authenticated `crypto_box_easy` encryption with random nonces and per-recipient public keys. The outer transport encryption remains in place. The host can see routing, timing, lengths and both connected IP addresses. A host that accepts a room offer is an intended recipient and receives a separate encrypted copy.

Public keys still come from the host, as they do for existing private messaging. This protects content from an ordinary relay, but is **not independent authentication against a malicious host substituting keys**. No new identity verification or key-pinning scheme is implied. File contents may contain identifying metadata.

File lengths, chunk offsets and acknowledgments use 64-bit fields. Protocol version is 26; update both host and clients.

## Validation

Local validation sources are kept under the ignored `build/validation/` directory, outside the application project. `FileTransferTests.cpp` there is a standalone console regression test intended for a disposable working directory. Link against the bundled x64 libsodium library and Winsock, with the repository and libsodium include directories; enable assertions. AddressSanitizer runs cover simultaneous recipients, binary/empty/Unicode files, source locking, overwrite races, cancellation/withdrawal, disconnects, timeouts, quotas, malformed chunks, incorrect tokens and hashes, parser fuzz inputs and partial-file cleanup.

Additional integration checks run a host and two clients in separate processes, verify exact received bytes, exercise simultaneous voice traffic, and confirm the relay host cannot decrypt messages addressed between clients. The renderer and chat click path are exercised with an asynchronous simulated save-picker result; the native Windows picker is not automated by that test.
