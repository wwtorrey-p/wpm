#requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [System.Net.IPAddress]$Ip,

    [Parameter(Mandatory)]
    [PSCredential]$Credential,

    [Parameter(Mandatory)]
    [ValidateSet('Get', 'SetSettings', 'Firmware')]
    [string]$Command,

    [string]$Endpoint = '/api/status',
    [hashtable]$Settings,
    [string]$File = (Join-Path $PSScriptRoot '..\.pio\build\esp32s3\firmware.bin'),
    [switch]$SkipCertificateCheck
)

$ErrorActionPreference = 'Stop'

if ($Ip.AddressFamily -ne [System.Net.Sockets.AddressFamily]::InterNetwork) {
    throw 'The device IP must be an IPv4 address.'
}
if ($Command -ne 'Get' -and $PSBoundParameters.ContainsKey('Endpoint')) {
    throw '-Endpoint is only valid with -Command Get.'
}
if ($Command -ne 'SetSettings' -and $PSBoundParameters.ContainsKey('Settings')) {
    throw '-Settings is only valid with -Command SetSettings.'
}
if ($Command -ne 'Firmware' -and $PSBoundParameters.ContainsKey('File')) {
    throw '-File is only valid with -Command Firmware.'
}

$request = @{
    Authentication = 'Basic'
    Credential = $Credential
    MaximumRedirection = 0
    ErrorAction = 'Stop'
}
if ($SkipCertificateCheck) {
    $request.SkipCertificateCheck = $true
}

switch ($Command) {
    'Get' {
        if ($Endpoint -notmatch '^/api/[a-zA-Z0-9_/-]+(?:\?[^#\s]*)?$') {
            throw 'Endpoint must be an /api/ path, optionally followed by a query string.'
        }
        $request.Uri = "https://${Ip}${Endpoint}"
        $request.Method = 'Get'
    }
    'SetSettings' {
        if (-not $Settings -or $Settings.Count -eq 0) {
            throw '-Settings must contain at least one setting.'
        }
        $allowed = @('initialBackoffH', 'maxBackoffH', 'graceS', 'gpm', 'targetGalPerDay',
                     'lowLevelGal', 'highLevelGal', 'maxRunM', 'runBackoffM', 'waterAddGal')
        foreach ($key in $Settings.Keys) {
            if ($key -cnotin $allowed) {
                throw "Unknown setting '$key'."
            }
            if ($Settings[$key] -isnot [byte] -and $Settings[$key] -isnot [int16] -and
                $Settings[$key] -isnot [int32] -and $Settings[$key] -isnot [int64] -and
                $Settings[$key] -isnot [single] -and $Settings[$key] -isnot [double] -and
                $Settings[$key] -isnot [decimal]) {
                throw "Setting '$key' must be a number."
            }
            if ([double]::IsNaN([double]$Settings[$key]) -or [double]::IsInfinity([double]$Settings[$key])) {
                throw "Setting '$key' must be a finite number."
            }
        }
        $request.Uri = "https://${Ip}/api/settings"
        $request.Method = 'Put'
        $request.ContentType = 'application/json; charset=utf-8'
        $request.Body = [System.Text.Encoding]::UTF8.GetBytes((ConvertTo-Json -InputObject $Settings -Compress))
    }
    'Firmware' {
        if (-not (Test-Path -LiteralPath $File -PathType Leaf)) {
            throw "Firmware file not found: $File"
        }
        $request.Uri = "https://${Ip}/api/firmware"
        $request.Method = 'Post'
        $request.ContentType = 'application/octet-stream'
        $request.InFile = (Resolve-Path -LiteralPath $File).ProviderPath
    }
}

$response = Invoke-WebRequest @request
if ("$($response.Headers['Content-Type'])" -match '(?i)\bapplication/json\b') {
    ConvertFrom-Json -InputObject $response.Content
}
else {
    $response.Content
}
