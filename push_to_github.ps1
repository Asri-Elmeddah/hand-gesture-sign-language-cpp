# One-click private GitHub push helper.
# Reads credentials from .env (never hardcoded in tracked sources).
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

$auth = "x-access-token:$token"
$basic = [Convert]::ToBase64String([Text.Encoding]::ASCII.GetBytes($auth))
$headers = @{
    Authorization = "Bearer $token"
    Accept        = "application/vnd.github+json"
    "X-GitHub-Api-Version" = "2022-11-28"
}

$user = Invoke-RestMethod -Uri "https://api.github.com/user" -Headers $headers
Write-Host "Authenticated as: $($user.login)"

$repoUrl = "https://api.github.com/repos/$($user.login)/$repoName"
try {
    Invoke-RestMethod -Uri $repoUrl -Headers $headers | Out-Null
    Write-Host "Repository $repoName already exists."
} catch {
    $body = @{ name = $repoName; private = $true; description = "AI-Powered Sign Language Translator and Automatic Control Telemetry (C++17 / OpenCV 4)" } | ConvertTo-Json
    Invoke-RestMethod -Method Post -Uri "https://api.github.com/user/repos" -Headers $headers -Body $body -ContentType "application/json" | Out-Null
    Write-Host "Created PRIVATE repository: $($user.login)/$repoName"
}

git -C $root remote remove origin 2>$null
git -C $root remote add origin "https://github.com/$($user.login)/$repoName.git"

$extraheader = "http.https://github.com/.extraheader=AUTHORIZATION: basic $basic"
git -C $root -c $extraheader push -u origin main
if ($LASTEXITCODE -eq 0) {
    Write-Host "Pushed main -> private repo https://github.com/$($user.login)/$repoName" -ForegroundColor Green
} else {
    Write-Host "Push failed. Check token scopes (repo / Contents: read+write)." -ForegroundColor Red
}
