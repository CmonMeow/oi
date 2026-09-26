param([switch]$Initialize)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSHOME 'Modules\Microsoft.PowerShell.Security\Microsoft.PowerShell.Security.psd1')
Import-Module (Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\Modules\PKI\PKI.psd1')
$identityFile = Join-Path $PSScriptRoot 'SigningThumbprint.txt'
if (Test-Path -LiteralPath $identityFile) {
    $thumbprint = (Get-Content -LiteralPath $identityFile -Raw).Trim()
    if ($thumbprint -notmatch '^[0-9A-Fa-f]{40}$') { throw 'Invalid SigningThumbprint.txt.' }
    $certificate = Get-Item -LiteralPath "Cert:\CurrentUser\My\$thumbprint"
} else {
    $candidates = @(Get-ChildItem Cert:\CurrentUser\My | Where-Object { $_.Subject -eq 'CN=oi Private Signing' -and $_.HasPrivateKey -and $_.NotAfter -gt (Get-Date) -and $_.NotBefore -le (Get-Date) })
    if ($candidates.Count -gt 1) { throw 'Multiple oi signing identities exist. Put the intended certificate thumbprint in SigningThumbprint.txt.' }
    if ($candidates.Count -eq 1) { $certificate = $candidates[0] }
    elseif ($Initialize) {
        $certificate = New-SelfSignedCertificate -Type CodeSigningCert -Subject 'CN=oi Private Signing' -FriendlyName 'oi private release signing' -CertStoreLocation 'Cert:\CurrentUser\My' -KeyAlgorithm RSA -KeyLength 3072 -HashAlgorithm SHA256 -KeyExportPolicy Exportable -NotAfter (Get-Date).AddYears(3)
    } else { throw 'No oi signing identity found. Restore your PFX, or run SignRelease.ps1 -Initialize for a new identity.' }
    Set-Content -LiteralPath $identityFile -Value $certificate.Thumbprint -Encoding ASCII
}
if (!$certificate.HasPrivateKey -or $certificate.NotAfter -le (Get-Date) -or $certificate.NotBefore -gt (Get-Date)) { throw 'Signing certificate is expired, not yet valid, or missing its private key.' }
$certificate
