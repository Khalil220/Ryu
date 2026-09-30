param(
    [switch]$Publish,
    [string]$VcpkgRoot = $env:VCPKG_ROOT
)

$ErrorActionPreference = 'Stop'
$repository = 'Khalil220/Ryu'
$root = Split-Path -Parent $PSScriptRoot

if (-not $VcpkgRoot -or -not (Test-Path (Join-Path $VcpkgRoot 'scripts\buildsystems\vcpkg.cmake'))) {
    throw 'Set VCPKG_ROOT, or pass -VcpkgRoot, to the folder of your vcpkg clone.'
}
$env:VCPKG_ROOT = $VcpkgRoot

function Invoke-Native {
    param([string]$Command, [string[]]$Arguments)
    $ErrorActionPreference = 'Continue'
    & $Command @Arguments 2>&1 | ForEach-Object { "$_" }
}

function Invoke-Checked {
    param([string]$Command, [string[]]$Arguments)
    Invoke-Native $Command $Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Command $($Arguments -join ' ') failed with exit code $LASTEXITCODE"
    }
}

Push-Location $root
try {
    if (git status --porcelain) {
        throw 'The working tree has uncommitted changes. Commit or stash them first.'
    }
    $match = [regex]::Match((Get-Content CMakeLists.txt -Raw), 'project\(Ryu VERSION (\d+\.\d+\.\d+)')
    if (-not $match.Success) {
        throw 'Could not find the version in CMakeLists.txt.'
    }
    $version = $match.Groups[1].Value
    $tag = "v$version"
    if (git tag --list $tag) {
        throw "$tag already exists. Raise the version in CMakeLists.txt first."
    }
    if ($Publish) {
        Invoke-Checked git @('fetch', 'origin', 'main')
        git merge-base --is-ancestor HEAD origin/main
        if ($LASTEXITCODE -ne 0) {
            throw 'HEAD is not on origin/main yet. Push main before publishing.'
        }
    }

    $work = Join-Path ([IO.Path]::GetTempPath()) "ryu-release-$version"
    if (Test-Path $work) {
        Invoke-Native git @('worktree', 'remove', '--force', $work) | Out-Null
        Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
    }
    Invoke-Checked git @('worktree', 'add', '--detach', $work, 'HEAD')
    try {
        Push-Location $work
        try {
            Invoke-Checked cmake @('--preset', 'windows')
            Invoke-Checked cmake @('--build', '--preset', 'release')
            Invoke-Checked ctest @('--test-dir', 'build/windows', '-C', 'Release', '-L', 'unit', '--output-on-failure')
        } finally {
            Pop-Location
        }

        $dist = Join-Path $root 'dist'
        New-Item -ItemType Directory -Force $dist | Out-Null
        $name = "Ryu-$version-win64.zip"
        $package = Join-Path $dist $name
        $binaries = Join-Path $work 'build\windows\src\app\Release'
        Compress-Archive -Force -DestinationPath $package -Path @(
            (Join-Path $binaries 'ryu.exe'),
            (Join-Path $binaries 'libmpv-2.dll'),
            (Join-Path $binaries 'prism.dll')
        )
        $hash = (Get-FileHash -Algorithm SHA256 $package).Hash.ToLowerInvariant()
        $sums = Join-Path $dist 'SHA256SUMS'
        [IO.File]::WriteAllText($sums, "$hash  $name`n")
        Write-Host "Built $package"
    } finally {
        Invoke-Native git @('worktree', 'remove', '--force', $work)
    }

    if ($Publish) {
        $previous = git tag --list 'v*' --sort=-v:refname | Select-Object -First 1
        $notes = if ($previous) {
            (git log --pretty='- %s' "$previous..HEAD") -join "`n"
        } else {
            'Initial release.'
        }
        $notesFile = Join-Path $dist 'notes.md'
        [IO.File]::WriteAllText($notesFile, "$notes`n")
        Invoke-Checked git @('tag', '-a', $tag, '-m', "Ryu $version")
        Invoke-Checked git @('push', 'origin', $tag)
        Invoke-Checked gh @('release', 'create', $tag, $package, $sums, '--repo', $repository, '--title', "Ryu $version",
            '--notes-file', $notesFile)
        Write-Host "Published $tag"
    } else {
        Write-Host 'Run with -Publish to tag and upload this release.'
    }
} finally {
    Pop-Location
}
