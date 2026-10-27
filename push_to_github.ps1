# One-click private GitHub push helper.
# Reads credentials from .env (never hardcoded in tracked sources).
# Uses curl.exe with forced IPv4 (api.github.com IPv6 path is broken on some networks).
# Usage:  powershell -ExecutionPolicy Bypass -File push_to_github.ps1
$ErrorActionPreference = "Stop"
$root = $PSScriptRoot

$envMap = @{}
Get-Content (Join-Path $root ".env") | ForEach-Object {
    $line = $_.Trim()
    if ($line -and -not $line.StartsWith("#") -and $line -match "^([^=]+)=(.*)$") {
        $envMap[$Matches[1].Trim()] = $Matches[2].Trim().Trim('"')
    }
}

$token = $envMap["GITHUB_PAT_TOKEN"]
$repoName = $envMap["GITHUB_REPO_NAME"]
if (-not $token -or $token -eq "PASTE_YOUR_GITHUB_TOKEN_HERE") {
    Write-Host "ERROR: paste your real GitHub token into .env (GITHUB_PAT_TOKEN) and re-run." -ForegroundColor Red
    exit 1
}
if (-not $repoName) { $repoName = "hand-gesture-sign-language-cpp" }

function Invoke-GitHubApi {
    param([string]$Method, [string]$Uri, [string]$Body)
    $args = @("-4", "-s", "-X", $Method, $Uri,
              "-H", "Authorization: Bearer $token",
              "-H", "Accept: application/vnd.github+json",
              "-H", "X-GitHub-Api-Version: 2022-11-28",
              "-w", "`n%{http_code}")
    if ($Body) {
        $args += @("-H", "Content-Type: application/json", "--data", $Body)
    }
    $out = $null
    for ($attempt = 1; $attempt -le 3; $attempt++) {
        $out = & curl.exe @args 2>$null
        if ($out -and $out.Count -gt 0) { break }
        Start-Sleep -Seconds 2
    }
    if (-not $out) { throw "curl could not reach $Uri" }
    $code = [int]$out[-1]
    $json = ($out[0..($out.Count - 2)]) -join "`n"
    return @{ Code = $code; Json = $json }
}

$userResp = Invoke-GitHubApi -Method GET -Uri "https://api.github.com/user"
if ($userResp.Code -ne 200) {
    Write-Host "ERROR: GitHub authentication failed (HTTP $($userResp.Code)). Check GITHUB_PAT_TOKEN in .env." -ForegroundColor Red
    exit 1
}
$user = $userResp.Json | ConvertFrom-Json
Write-Host "Authenticated as: $($user.login)"

$repoResp = Invoke-GitHubApi -Method GET -Uri "https://api.github.com/repos/$($user.login)/$repoName"
if ($repoResp.Code -eq 200) {
    Write-Host "Repository $repoName already exists."
} else {
    $body = @{ name = $repoName; private = $true; description = "AI-Powered Sign Language Translator and Automatic Control Telemetry (C++17 / OpenCV 4)" } | ConvertTo-Json
    $createResp = Invoke-GitHubApi -Method POST -Uri "https://api.github.com/user/repos" -Body $body
    if ($createResp.Code -ne 201) {
        Write-Host "ERROR: repo creation failed (HTTP $($createResp.Code)): $($createResp.Json.Substring(0, [Math]::Min(300, $createResp.Json.Length)))" -ForegroundColor Red
        exit 1
    }
    Write-Host "Created PRIVATE repository: $($user.login)/$repoName"
}

& git -C $root remote remove origin 2>$null
& git -C $root remote add origin "https://github.com/$($user.login)/$repoName.git"

$auth = "x-access-token:$token"
$basic = [Convert]::ToBase64String([Text.Encoding]::ASCII.GetBytes($auth))
$extraheader = "http.https://github.com/.extraheader=AUTHORIZATION: basic $basic"
& git -C $root -c $extraheader push -u origin main
if ($LASTEXITCODE -eq 0) {
    Write-Host "Pushed main -> private repo https://github.com/$($user.login)/$repoName" -ForegroundColor Green
} else {
    Write-Host "Push failed. Check token scopes (repo / Contents: read+write)." -ForegroundColor Red
    exit 1
}
