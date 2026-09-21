[CmdletBinding()]
param(
    [switch]$InstallOnly,

    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$DshArgs
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$NodeVersion = '24.16.0'
$ProjectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$RuntimeRoot = Join-Path $ProjectRoot '.runtime'
$NodeRoot = Join-Path $RuntimeRoot 'node'
$NodeExe = Join-Path $NodeRoot 'node.exe'
$NpmCli = Join-Path $NodeRoot 'node_modules\npm\bin\npm-cli.js'
$DshCli = Join-Path $ProjectRoot 'node_modules\@deepseek-ai\dsh\lib\bin.js'
$ElectronExe = Join-Path $ProjectRoot 'node_modules\electron\dist\electron.exe'
$ElectronInstall = Join-Path $ProjectRoot 'node_modules\electron\install.js'
$PackageLock = Join-Path $ProjectRoot 'package-lock.json'
$NpmUserConfig = Join-Path $ProjectRoot '.npmrc'
$WinToolsHelper = Join-Path $ProjectRoot 'bin\dsh-wintools-helper.exe'

function Assert-ProjectPath {
    param([Parameter(Mandatory = $true)][string]$Path)

    $projectPrefix = $ProjectRoot.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    $resolved = [IO.Path]::GetFullPath($Path)
    if (-not $resolved.StartsWith($projectPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to modify a path outside the project: $resolved"
    }
}

function Remove-ProjectDirectory {
    param([Parameter(Mandatory = $true)][string]$Path)

    Assert-ProjectPath -Path $Path
    if (-not (Test-Path -LiteralPath $Path)) {
        return
    }

    if (Test-Path -LiteralPath $WinToolsHelper) {
        & $WinToolsHelper remove-tree $Path
        if ($LASTEXITCODE -eq 0) {
            return
        }
    }

    Remove-Item -LiteralPath $Path -Recurse -Force
}

function Get-NodeArchiveName {
    $architecture = if ($env:PROCESSOR_ARCHITECTURE -eq 'ARM64') { 'arm64' } else { 'x64' }
    return "node-v$NodeVersion-win-$architecture"
}

function Get-FileSha256 {
    param([Parameter(Mandatory = $true)][string]$Path)

    $stream = [IO.File]::OpenRead($Path)
    try {
        $sha256 = [Security.Cryptography.SHA256]::Create()
        try {
            $hashBytes = $sha256.ComputeHash($stream)
            return ([BitConverter]::ToString($hashBytes) -replace '-', '').ToLowerInvariant()
        }
        finally {
            $sha256.Dispose()
        }
    }
    finally {
        $stream.Dispose()
    }
}

function Invoke-DownloadWithFallback {
    param(
        [Parameter(Mandatory = $true)][string[]]$Urls,
        [Parameter(Mandatory = $true)][string]$Destination
    )

    $lastError = $null
    foreach ($url in $Urls) {
        try {
            Write-Host "Downloading $url"
            Invoke-WebRequest -UseBasicParsing -Uri $url -OutFile $Destination
            return
        }
        catch {
            $lastError = $_
            if (Test-Path -LiteralPath $Destination) {
                Remove-Item -LiteralPath $Destination -Force
            }
        }
    }

    throw "All download mirrors failed. Last error: $lastError"
}

function Install-PortableNode {
    $archiveName = Get-NodeArchiveName
    $downloadRoot = Join-Path $ProjectRoot '.cache\downloads'
    $archivePath = Join-Path $downloadRoot "$archiveName.zip"
    $checksumPath = Join-Path $downloadRoot "SHASUMS256-$NodeVersion.txt"
    $extractRoot = Join-Path $ProjectRoot '.cache\node-extract'
    $downloadUrls = @(
        "https://nodejs.org/dist/v$NodeVersion/$archiveName.zip",
        "https://npmmirror.com/mirrors/node/v$NodeVersion/$archiveName.zip"
    )
    $checksumUrls = @(
        "https://nodejs.org/dist/v$NodeVersion/SHASUMS256.txt",
        "https://npmmirror.com/mirrors/node/v$NodeVersion/SHASUMS256.txt"
    )

    New-Item -ItemType Directory -Force -Path $downloadRoot | Out-Null

    if (-not (Test-Path -LiteralPath $archivePath)) {
        Write-Host "Downloading portable Node.js $NodeVersion..."
        Invoke-DownloadWithFallback -Urls $downloadUrls -Destination $archivePath
    }

    if (-not (Test-Path -LiteralPath $checksumPath)) {
        Invoke-DownloadWithFallback -Urls $checksumUrls -Destination $checksumPath
    }

    $checksumLine = Get-Content -LiteralPath $checksumPath |
        Where-Object { $_ -match "\s$([regex]::Escape("$archiveName.zip"))$" } |
        Select-Object -First 1
    if (-not $checksumLine) {
        throw "Could not find $archiveName.zip in the official Node.js checksum file."
    }

    $expectedHash = ($checksumLine -split '\s+')[0].ToLowerInvariant()
    $actualHash = Get-FileSha256 -Path $archivePath
    if ($actualHash -ne $expectedHash) {
        throw "Node.js archive checksum mismatch: expected $expectedHash, got $actualHash"
    }

    Remove-ProjectDirectory -Path $extractRoot
    Remove-ProjectDirectory -Path $NodeRoot

    New-Item -ItemType Directory -Force -Path $extractRoot | Out-Null
    Expand-Archive -LiteralPath $archivePath -DestinationPath $extractRoot -Force

    $expandedNodeRoot = Join-Path $extractRoot $archiveName
    if (-not (Test-Path -LiteralPath (Join-Path $expandedNodeRoot 'node.exe'))) {
        throw "The downloaded Node.js archive did not contain node.exe."
    }

    New-Item -ItemType Directory -Force -Path $RuntimeRoot | Out-Null
    Move-Item -LiteralPath $expandedNodeRoot -Destination $NodeRoot
}

function Enter-PortableEnvironment {
    $names = @(
        'PATH',
        'DSH_HOME',
        'NPM_CONFIG_CACHE',
        'NPM_CONFIG_PREFIX',
        'NPM_CONFIG_USERCONFIG',
        'NPM_CONFIG_UPDATE_NOTIFIER',
        'ELECTRON_MIRROR',
        'electron_config_cache'
    )
    $saved = @{}
    foreach ($name in $names) {
        $saved[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
    }

    $env:PATH = "$NodeRoot;$ProjectRoot\node_modules\.bin;$($saved['PATH'])"
    $env:DSH_HOME = Join-Path $ProjectRoot '.data\dsh'
    $env:NPM_CONFIG_CACHE = Join-Path $ProjectRoot '.cache\npm'
    $env:NPM_CONFIG_PREFIX = Join-Path $ProjectRoot '.runtime\npm-global'
    $env:NPM_CONFIG_USERCONFIG = $NpmUserConfig
    $env:NPM_CONFIG_UPDATE_NOTIFIER = 'false'
    $env:ELECTRON_MIRROR = 'https://npmmirror.com/mirrors/electron/'
    $env:electron_config_cache = Join-Path $ProjectRoot '.cache\electron'

    return $saved
}

function Exit-PortableEnvironment {
    param([Parameter(Mandatory = $true)][hashtable]$Saved)

    foreach ($name in $Saved.Keys) {
        if ($null -eq $Saved[$name]) {
            Remove-Item -LiteralPath "Env:$name" -ErrorAction SilentlyContinue
        }
        else {
            Set-Item -LiteralPath "Env:$name" -Value $Saved[$name]
        }
    }
}

if ((-not (Test-Path -LiteralPath $NodeExe)) -or (-not (Test-Path -LiteralPath $NpmCli))) {
    Install-PortableNode
}

$savedEnvironment = Enter-PortableEnvironment
$exitCode = 0

try {
    if ((-not (Test-Path -LiteralPath $DshCli)) -or $InstallOnly) {
        if (Test-Path -LiteralPath $PackageLock) {
            Write-Host 'Installing the pinned Harness dependency tree...'
            & $NodeExe $NpmCli ci --no-audit --no-fund
        }
        else {
            Write-Host 'Creating package-lock.json and installing Harness...'
            & $NodeExe $NpmCli install --no-audit --no-fund
        }

        if ($LASTEXITCODE -ne 0) {
            throw "npm exited with code $LASTEXITCODE"
        }

        if (-not (Test-Path -LiteralPath $ElectronExe)) {
            if (-not (Test-Path -LiteralPath $ElectronInstall)) {
                throw "Electron install script was not found: $ElectronInstall"
            }

            Write-Host 'Downloading Electron from the configured mirror...'
            & $NodeExe $ElectronInstall
            if ($LASTEXITCODE -ne 0) {
                throw "Electron installation exited with code $LASTEXITCODE"
            }
        }
    }

    if (-not $InstallOnly) {
        if (($null -eq $DshArgs) -or ($DshArgs.Count -eq 0)) {
            $DshArgs = @('web')
        }

        & $NodeExe $DshCli @DshArgs
        $exitCode = $LASTEXITCODE
    }
}
finally {
    Exit-PortableEnvironment -Saved $savedEnvironment
}

exit $exitCode
