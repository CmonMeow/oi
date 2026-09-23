param([string]$Executable, [switch]$Initialize)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSHOME 'Modules\Microsoft.PowerShell.Security\Microsoft.PowerShell.Security.psd1')
Import-Module (Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\Modules\PKI\PKI.psd1')
$signingDirectory = Join-Path $env:LOCALAPPDATA 'oi\Signing'
$identityFile = Join-Path $signingDirectory 'thumbprint.txt'
if ($Initialize -and !(Test-Path -LiteralPath $identityFile)) {
    New-Item -ItemType Directory -Path $signingDirectory -Force | Out-Null
    $certificate = New-SelfSignedCertificate -Type CodeSigningCert -Subject 'CN=oi Private Signing' -FriendlyName 'oi private release signing' -CertStoreLocation 'Cert:\CurrentUser\My' -KeyAlgorithm RSA -KeyLength 3072 -HashAlgorithm SHA256 -KeyExportPolicy Exportable -NotAfter (Get-Date).AddYears(3)
    Set-Content -LiteralPath $identityFile -Value $certificate.Thumbprint -Encoding ASCII
}
if (!(Test-Path -LiteralPath $identityFile)) { throw 'Run SignRelease.ps1 -Initialize once to configure private signing.' }
$thumbprint = (Get-Content -LiteralPath $identityFile -Raw).Trim()
if ($thumbprint -notmatch '^[0-9A-Fa-f]{40}$') { throw 'Invalid signing certificate thumbprint.' }
$certificate = Get-Item -LiteralPath "Cert:\CurrentUser\My\$thumbprint"
if (!$certificate.HasPrivateKey -or $certificate.NotAfter -le (Get-Date) -or $certificate.NotBefore -gt (Get-Date)) { throw 'Signing certificate is expired, not yet valid, or missing its private key.' }
# Export only the public certificate, never the private key. Do not install trust.
Export-Certificate -Cert $certificate -FilePath (Join-Path $signingDirectory 'oi-signing.cer') -Force | Out-Null
if (!$Executable) { if ($Initialize) { Write-Output "Private signing configured: $thumbprint"; exit 0 }; throw 'Executable is required.' }
$target = (Resolve-Path -LiteralPath $Executable).Path
function Test-ExpectedSignature($signature) {
    # CERT_E_UNTRUSTEDROOT is expected for this private identity; other errors are not.
    $untrustedRoot = (New-Object System.Security.Cryptography.CryptographicException -ArgumentList ([int]-2146762487)).Message
    return $signature.SignerCertificate -and $signature.SignerCertificate.Thumbprint -eq $thumbprint -and
        $signature.TimeStamperCertificate -and ($signature.Status -eq 'Valid' -or $signature.StatusMessage.Trim().TrimEnd('.') -eq $untrustedRoot.Trim().TrimEnd('.'))
}
$signature = Get-AuthenticodeSignature -LiteralPath $target
if (Test-ExpectedSignature $signature) { Write-Output 'Release already signed.'; exit 0 }
$kits = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
$tool = Get-ChildItem -Path "$kits\*\x64\signtool.exe" -File | Sort-Object FullName -Descending | Select-Object -First 1
if (!$tool) { throw 'Windows SDK SignTool was not found.' }
& $tool.FullName sign /fd SHA256 /s My /sha1 $thumbprint /tr http://timestamp.digicert.com /td SHA256 $target
if ($LASTEXITCODE -ne 0) { throw "SignTool failed: $LASTEXITCODE" }
$signature = Get-AuthenticodeSignature -LiteralPath $target
if (!(Test-ExpectedSignature $signature)) { throw "Signature verification failed: $($signature.Status) $($signature.StatusMessage)" }
Write-Output "Private signature applied and timestamped ($($signature.Status))."
