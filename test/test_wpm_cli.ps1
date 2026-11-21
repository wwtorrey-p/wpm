#requires -Version 7.0
$ErrorActionPreference = 'Stop'
$cli = Join-Path $PSScriptRoot '..\scripts\wpm-cli.ps1'
$credential = [PSCredential]::new('test', (ConvertTo-SecureString 'test' -AsPlainText -Force))
$requests = [System.Collections.Generic.List[object]]::new()

function Invoke-WebRequest {
    param($Uri, $Method, $Authentication, $Credential, $MaximumRedirection, $SkipCertificateCheck,
          $ContentType, $Body, $InFile)
    $requests.Add($PSBoundParameters)
    if ($Uri -like '*fail*') { throw 'HTTP 409: Pump is running' }
    if ($Method -eq 'Post') {
        return [pscustomobject]@{ Headers = @{}; Content = 'ok' }
    }
    return [pscustomobject]@{ Headers = @{ 'Content-Type' = 'application/json' }; Content = '{"result":42}' }
}

function Assert($Condition, $Message) {
    if (-not $Condition) { throw $Message }
}

function Assert-Throws($Action, $Expected) {
    try { & $Action }
    catch {
        if ($_.Exception.Message -like "*$Expected*") { return }
        throw
    }
    throw "Expected an error containing '$Expected'."
}

$result = & $cli -Ip 127.0.0.1 -Credential $credential -Command Get
$request = $requests[-1]
Assert ($request.Uri -eq 'https://127.0.0.1/api/status') "GET must default to the status endpoint; got '$($request.Uri)'."
Assert ($request.Method -eq 'Get' -and $request.Authentication -eq 'Basic') 'GET must use Basic authentication.'
Assert (-not $request.ContainsKey('SkipCertificateCheck')) 'TLS verification must be enabled by default.'
Assert ($request.MaximumRedirection -eq 0 -and $result.result -eq 42) 'GET must disallow redirects and parse JSON.'

& $cli -Ip 127.0.0.1 -Credential $credential -Command Get `
    -Endpoint '/api/flo/usage?days=7&interval=1d' -SkipCertificateCheck | Out-Null
$request = $requests[-1]
Assert ($request.Uri -eq 'https://127.0.0.1/api/flo/usage?days=7&interval=1d') 'GET must preserve the query.'
Assert $request.SkipCertificateCheck 'The explicit certificate override must be passed through.'

& $cli -Ip 127.0.0.1 -Credential $credential -Command SetSettings -Settings @{ gpm = 5; maxRunM = 10 } | Out-Null
$request = $requests[-1]
$body = [Text.Encoding]::UTF8.GetString($request.Body) | ConvertFrom-Json
Assert ($request.Uri -eq 'https://127.0.0.1/api/settings' -and $request.Method -eq 'Put') 'Settings must use PUT.'
Assert ($body.gpm -eq 5 -and $body.maxRunM -eq 10) 'Settings JSON must contain the supplied values.'

$file = [IO.Path]::GetTempFileName()
try {
    [IO.File]::WriteAllBytes($file, [byte[]](1, 2, 3))
    $result = & $cli -Ip 127.0.0.1 -Credential $credential -Command Firmware -File $file
    $request = $requests[-1]
    Assert ($request.Uri -eq 'https://127.0.0.1/api/firmware' -and $request.Method -eq 'Post') 'Firmware must use POST.'
    Assert ($request.InFile -eq $file -and $request.ContentType -eq 'application/octet-stream' -and $result -eq 'ok') 'Firmware must upload the selected file.'
}
finally {
    Remove-Item -LiteralPath $file
}

Assert-Throws { & $cli -Ip 127.0.0.1 -Credential $credential -Command Get -Endpoint 'https://example.com' } 'Endpoint must'
Assert-Throws { & $cli -Ip 127.0.0.1 -Credential $credential -Command SetSettings -Settings @{ typo = 1 } } 'Unknown setting'
Assert-Throws { & $cli -Ip 127.0.0.1 -Credential $credential -Command SetSettings -Settings @{ gpm = '5' } } 'must be a number'
Assert-Throws { & $cli -Ip 127.0.0.1 -Credential $credential -Command SetSettings -Settings @{ gpm = [double]::NaN } } 'finite number'
Assert-Throws { & $cli -Ip 127.0.0.1 -Credential $credential -Command Firmware -File $file } 'Firmware file not found'
Assert-Throws { & $cli -Ip 127.0.0.1 -Credential $credential -Command Get -Endpoint '/api/fail' } 'HTTP 409'
Write-Host 'CLI tests passed.'
