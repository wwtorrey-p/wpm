[CmdletBinding()]
param(
    [ValidateSet('Build', 'Test', 'Web', 'Audit', 'Analyze', 'Secrets', 'Config', 'Security', 'Release')]
    [string[]]$Task = @('Build'),

    [switch]$RequireHardenedConfig
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$web = Join-Path $root 'web'
$failures = [System.Collections.Generic.List[string]]::new()

function Find-Command([string]$Name, [string]$Fallback = '') {
    $command = Get-Command $Name -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($command) { return $command.Source }
    if ($Fallback -and (Test-Path -LiteralPath $Fallback -PathType Leaf)) { return $Fallback }
    throw "Required tool '$Name' was not found. See README.md for setup."
}

function Invoke-External([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "'$Program' exited with code $LASTEXITCODE."
    }
}

function Invoke-Step([string]$Name, [scriptblock]$Action) {
    Write-Host "`n=== $Name ==="
    try {
        & $Action
        Write-Host "PASS: $Name"
    }
    catch {
        Write-Host "FAIL: $Name - $_" -ForegroundColor Red
        $failures.Add($Name)
    }
}

function Get-PlatformIO {
    $fallback = Join-Path $env:USERPROFILE '.platformio\penv\Scripts\pio.exe'
    Find-Command 'pio' $fallback
}

function Test-FirmwareConfig {
    $config = Join-Path $root 'sdkconfig.esp32s3'
    if (-not (Test-Path -LiteralPath $config -PathType Leaf)) {
        throw 'sdkconfig.esp32s3 does not exist. Build the firmware first to generate the effective configuration.'
    }
    $lines = Get-Content -LiteralPath $config
    $missing = @()
    foreach ($setting in @('CONFIG_SECURE_BOOT', 'CONFIG_SECURE_FLASH_ENC_ENABLED')) {
        if ($lines -contains "$setting=y") {
            Write-Host "$setting is enabled in the generated configuration."
        }
        else {
            Write-Warning "$setting is not enabled in the generated configuration."
            $missing += $setting
        }
    }
    Write-Warning 'Configuration alone does not verify device eFuses, provisioning, TLS, authentication, or OTA security; review them separately.'
    if ($RequireHardenedConfig -and $missing.Count -gt 0) {
        throw "Required security settings are disabled or absent: $($missing -join ', ')."
    }
}

$selected = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
foreach ($item in $Task) {
    switch ($item) {
        'Release' {
            foreach ($step in @('Build', 'Test', 'Audit', 'Analyze', 'Secrets', 'Config')) {
                [void]$selected.Add($step)
            }
        }
        'Security' {
            foreach ($step in @('Audit', 'Analyze', 'Secrets', 'Config')) {
                [void]$selected.Add($step)
            }
        }
        default { [void]$selected.Add($item) }
    }
}

if ($RequireHardenedConfig -and -not $selected.Contains('Config')) {
    throw '-RequireHardenedConfig requires the Config, Security, or Release task.'
}

Push-Location $root
try {
    if ($selected.Contains('Web')) {
        Invoke-Step 'Web build' {
            $npm = Find-Command 'npm.cmd'
            Push-Location $web
            try { Invoke-External $npm @('run', 'build') }
            finally { Pop-Location }
        }
    }
    if ($selected.Contains('Build')) {
        Invoke-Step 'Firmware build' {
            $pio = Get-PlatformIO
            $oldStrict = $env:WPM_STRICT_WEB_BUILD
            try {
                $env:WPM_STRICT_WEB_BUILD = '1'
                Invoke-External $pio @('run', '-e', 'esp32s3')
            }
            finally { $env:WPM_STRICT_WEB_BUILD = $oldStrict }
        }
    }
    if ($selected.Contains('Test')) {
        Invoke-Step 'Native tests' {
            Invoke-External (Get-PlatformIO) @('test', '-e', 'native')
        }
    }
    if ($selected.Contains('Audit')) {
        Invoke-Step 'Web dependency audit' {
            $npm = Find-Command 'npm.cmd'
            Push-Location $web
            try { Invoke-External $npm @('audit', '--audit-level=moderate') }
            finally { Pop-Location }
        }
    }
    if ($selected.Contains('Analyze')) {
        Invoke-Step 'Firmware static analysis' {
            Invoke-External (Get-PlatformIO) @('check', '-e', 'esp32s3', '--fail-on-defect', 'medium')
        }
    }
    if ($selected.Contains('Secrets')) {
        Invoke-Step 'Git history secrets scan' {
            $gitleaks = Find-Command 'gitleaks'
            Invoke-External $gitleaks @('git', '--log-opts=--all', '--redact=100', $root)
        }
        Invoke-Step 'Working directory secrets scan' {
            $gitleaks = Find-Command 'gitleaks'
            Invoke-External $gitleaks @('dir', '--redact=100', $root)
        }
    }
    if ($selected.Contains('Config')) {
        Invoke-Step 'Firmware security configuration' { Test-FirmwareConfig }
    }
}
finally { Pop-Location }

if ($failures.Count -gt 0) {
    Write-Host "`nFailed: $($failures -join ', ')" -ForegroundColor Red
    exit 1
}
Write-Host "`nAll selected steps passed."
