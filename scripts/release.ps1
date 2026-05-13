<#
Release packaging script

Creates a zip containing:
- data/best_pareto_model.onnx
- data/planets.csv
- data/resources.csv
- data/server_ids.csv
- data/geoscout.location_only.csv as data/geoscout.csv (falls back to data/geoscout.csv if missing)
- data/labelmap.json (accepts label_map.json or labelmap.json)
- everything under images/planets -> images/planets
- contents of cpp_engine\build_app\Release placed at the zip root
- an empty config/settings.ini

Usage (run from repository root or pass -RepoRoot):
.
  .\scripts\release.ps1 -OutputZip .\scout_release.zip

Parameters:
  -OutputZip: Path to produce zip (default: ./scout_release.zip)
  -RepoRoot: repository root (default: script directory)
  -BuildReleaseDir: relative path to Release build (default: cpp_engine\build_app\Release)
#>

param(
    [string]$OutputZip = "scout_release.zip",
    [string]$RepoRoot = "./",
    [string]$BuildReleaseDir = "cpp_engine\build_app\Release"
)

try {
    $scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
} catch {
    $scriptDir = $null
}

# Determine repository root. Prefer explicit -RepoRoot, else use current working directory where script was launched.
if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
    $candidate = (Get-Location).ProviderPath
} else {
    $candidate = $RepoRoot
}

try {
    $resolved = Resolve-Path -Path $candidate -ErrorAction Stop
    $repoRoot = $resolved.ProviderPath
} catch {
    Write-Host "Warning: could not resolve path '$candidate'. Falling back to current directory."
    $repoRoot = (Get-Location).ProviderPath
}

Write-Host "Repo root: $repoRoot"

$staging = Join-Path -Path ([System.IO.Path]::GetTempPath()) -ChildPath ("scout_release_" + (Get-Date -Format "yyyyMMddHHmmss"))
Write-Host "Creating staging directory: $staging"
New-Item -ItemType Directory -Force -Path $staging | Out-Null

$dataDir = Join-Path $staging 'data'
$imagesPlanetsDir = Join-Path $staging 'images\planets'
$configDir = Join-Path $staging 'config'
New-Item -ItemType Directory -Force -Path $dataDir, $imagesPlanetsDir, $configDir | Out-Null

function Copy-IfExists($src, $dest) {
    if (Test-Path $src) {
        $destDir = Split-Path -Parent $dest
        if (!(Test-Path $destDir)) { New-Item -ItemType Directory -Force -Path $destDir | Out-Null }
        Copy-Item -Path $src -Destination $dest -Recurse -Force
        Write-Host "Added: $src -> $dest"
        return $true
    } else {
        Write-Host "Missing: $src"
        return $false
    }
}

# Copy explicit data files
$files = @('data/best_pareto_model.onnx','data/planets.csv','data/resources.csv','data/server_ids.csv')
foreach ($f in $files) {
    $src = Join-Path $repoRoot $f
    $dest = Join-Path $dataDir (Split-Path $f -Leaf)
    Copy-IfExists $src $dest | Out-Null
}

# geoscout.location_only.csv -> data/geoscout.csv (fallback to data/geoscout.csv)
$geoAlt = Join-Path $repoRoot 'data/geoscout.location_only.csv'
$geo = Join-Path $repoRoot 'data/geoscout.csv'
if (Test-Path $geoAlt) {
    Copy-IfExists $geoAlt (Join-Path $dataDir 'geoscout.csv') | Out-Null
} elseif (Test-Path $geo) {
    Copy-IfExists $geo (Join-Path $dataDir 'geoscout.csv') | Out-Null
} else {
    Write-Host "Missing: geoscout source file (neither geoscout.location_only.csv nor geoscout.csv found)"
}

# label map (try common variants) -> data/labelmap.json
$labelCandidates = @('data/labelmap.json','data/label_map.json','data/label-map.json')
$foundLabel = $false
foreach ($c in $labelCandidates) {
    $src = Join-Path $repoRoot $c
    if (Test-Path $src) {
        Copy-IfExists $src (Join-Path $dataDir 'labelmap.json') | Out-Null
        $foundLabel = $true
        break
    }
}
if (-not $foundLabel) { Write-Host "Missing: label map file (tried: $($labelCandidates -join ', '))" }

# Copy images/planets recursively if present
$imagesSrc = Join-Path $repoRoot 'images\planets'
if (Test-Path $imagesSrc) {
    Copy-Item -Path (Join-Path $imagesSrc '*') -Destination $imagesPlanetsDir -Recurse -Force
    Write-Host "Added images/planets -> images/planets"
} else {
    Write-Host "Missing: images/planets"
}

# Copy Release build contents into zip root
$releaseSrc = Join-Path $repoRoot $BuildReleaseDir
if (Test-Path $releaseSrc) {
    Write-Host "Copying Release build contents from: $releaseSrc"
    Get-ChildItem -Path $releaseSrc -Force | ForEach-Object {
        $dest = Join-Path $staging $_.Name
        if ($_.PSIsContainer) {
            Copy-Item -Path $_.FullName -Destination $dest -Recurse -Force
        } else {
            Copy-Item -Path $_.FullName -Destination $dest -Force
        }
    }
} else {
    Write-Host "Missing: Release build directory: $releaseSrc"
}

# Add empty config/settings.ini
New-Item -ItemType File -Force -Path (Join-Path $configDir 'settings.ini') | Out-Null
Write-Host "Added empty config/settings.ini"

# Compose zip path
if ([System.IO.Path]::IsPathRooted($OutputZip)) {
    $zipFull = $OutputZip
} else {
    $zipFull = Join-Path $repoRoot $OutputZip
}

if (Test-Path $zipFull) { Remove-Item -Path $zipFull -Force }

Write-Host "Creating zip: $zipFull"
Compress-Archive -Path (Join-Path $staging '*') -DestinationPath $zipFull -Force

if (Test-Path $zipFull) {
    Write-Host "Created $zipFull"
    # cleanup
    Remove-Item -Path $staging -Recurse -Force
    Write-Host "Removed staging directory: $staging"
    exit 0
} else {
    Write-Host "Failed to create zip"
    exit 1
}
