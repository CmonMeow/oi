# Private release signing

Release x64 builds run SignRelease.ps1 after building oi.exe. SignTool applies
SHA-256 Authenticode signing and an RFC 3161 timestamp. Signing failure fails the
build. The timestamp service requires internet access. Version metadata lives in
rc.rc; the initial version is 0.1.0.0. Increment it for distributed releases.

Signing uses the current Windows user's Personal certificate store
(Cert:\CurrentUser\My, visible through certmgr.msc). The selected thumbprint and
public oi-signing.cer are under %LOCALAPPDATA%\oi\Signing. No private key is
stored in the repository. No certificate is installed as a trusted root.

A .cer contains only the public certificate and cannot restore signing ability.
A portable backup requires a certificate with an exportable private key and an
encrypted .pfx export. A non-exportable key cannot be backed up that way. Never
commit a PFX or its password, or send the private key to users. Preserve the exact
signing identity across releases; creating a replacement key changes that identity.

This is private signing: Windows may report NotTrusted until a recipient explicitly
trusts the certificate. It does not create public trust or resolve antivirus
classifications. The build process uses an execution-policy override for its own
PowerShell invocation only; it does not change the user's system execution policy.

## Back up this signing identity

The configured key is exportable. From the project folder, run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\BackupSigningKey.ps1 -Destination "$env:USERPROFILE\Desktop\oi-signing-backup.pfx"
```

The script prompts for a password and exports an AES-256/SHA-256 protected PFX.
Move it to secure offline storage; keep the password separately. A backup has not
been created until this step succeeds. To restore on another PC, import the PFX
with its password into Current User / Personal using certmgr.msc, and restore the
same thumbprint.txt to %LOCALAPPDATA%\oi\Signing. Do not run Initialize to create
a different identity when restoring. The public .cer alone is insufficient.

Verified: Release build signs automatically, a repeat build preserves the existing
signature, and a modified test copy returns HashMismatch. Public trust remains
unchanged; this self-signed certificate is expected to report an untrusted root.
