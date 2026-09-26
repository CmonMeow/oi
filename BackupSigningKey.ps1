param([Parameter(Mandatory=$true)][string]$Destination)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSHOME 'Modules\Microsoft.PowerShell.Security\Microsoft.PowerShell.Security.psd1')
Import-Module (Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\Modules\PKI\PKI.psd1')
$certificate = & (Join-Path $PSScriptRoot 'GetSigningCertificate.ps1')
if (Test-Path -LiteralPath $Destination) { throw 'Choose a new backup filename; existing files will not be overwritten.' }
if ([IO.Path]::GetExtension($Destination) -ne '.pfx') { throw 'Use a .pfx filename.' }
$password = Read-Host 'Enter a strong password for the encrypted signing-key backup' -AsSecureString
if ($password.Length -lt 12) { throw 'Use at least 12 characters.' }
Export-PfxCertificate -Cert $certificate -FilePath $Destination -Password $password -CryptoAlgorithmOption AES256_SHA256 -ChainOption EndEntityCertOnly -NoClobber | Out-Null
$password.Dispose()
Write-Output 'Encrypted private-key backup created. Keep the PFX and its password secure and separate.'
