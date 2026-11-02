# SPDX-License-Identifier: MIT
[CmdletBinding()]
param(
    [string]$ProjectRoot = (Split-Path $PSScriptRoot -Parent),
    [string]$ArchivePath,
    [switch]$Download
)
$ErrorActionPreference = 'Stop'
$projectPath = [IO.Path]::GetFullPath($ProjectRoot)
if (!(Test-Path -LiteralPath $projectPath -PathType Container)) {
    throw 'ProjectRoot must contain exactly one .uproject file.'
}
$projectFiles = @(Get-ChildItem -LiteralPath $projectPath -Filter '*.uproject' -File)
if ($projectFiles.Count -ne 1) {
    throw 'ProjectRoot must contain exactly one .uproject file.'
}
$packageName = '2026.10.16_UE5.8_DLSS4.5Plugin_v8.9.0.zip'
$packageHash = 'ADEEB59B9635794E05252438BF01307BF8A629769E8F0517237363F690BB380C'
$packageUrl = 'https://developer.nvidia.com/downloads/assets/gameworks/downloads/secure/dlss/ue-dlss-5.8/8.9.0/2026.10.16_ue5.8_dlss4.5plugin_v8.9.0.zip'
$cachePath = Join-Path $projectPath 'Saved/ThirdParty/NVIDIA'
if (!$ArchivePath) { $ArchivePath = Join-Path $cachePath $packageName }
$ArchivePath = [IO.Path]::GetFullPath($ArchivePath)
if (!(Test-Path -LiteralPath $ArchivePath -PathType Leaf)) {
    if (!$Download) { throw 'Official SDK archive is missing. Supply -ArchivePath or use -Download after reviewing the NVIDIA license.' }
    [IO.Directory]::CreateDirectory((Split-Path $ArchivePath -Parent)) | Out-Null
    $partialPath = "$ArchivePath.part"
    Invoke-WebRequest -Uri $packageUrl -OutFile $partialPath
    if ((Get-FileHash -LiteralPath $partialPath -Algorithm SHA256).Hash -ne $packageHash) { throw 'Official archive SHA256 mismatch. The partial file has been preserved.' }
    [IO.File]::Move($partialPath, $ArchivePath)
}
if ((Get-FileHash -LiteralPath $ArchivePath -Algorithm SHA256).Hash -ne $packageHash) { throw 'Official archive SHA256 mismatch. No project files changed.' }

$vendorPath = Join-Path $projectPath 'Plugins/NVIDIA'
$stagingPath = Join-Path $cachePath ('Prepared-' + [Guid]::NewGuid().ToString('N'))
$files = [Collections.Generic.List[object]]::new()
$archive = [IO.Compression.ZipFile]::OpenRead($ArchivePath)
try {
    foreach ($entry in $archive.Entries) {
        $name = $entry.FullName
        if ($name.EndsWith('/') -or $name -notmatch '^Plugins/(DLSS|StreamlineNGXCommon)/') { continue }
        if ($name.Contains('/Intermediate/')) { continue }
        if ($name.Contains('/Binaries/') -and $name -notmatch '/Binaries/ThirdParty/Win64/(nvngx_dlss|nvngx_dlssd)\.dll$') { continue }
        $relative = $name.Substring('Plugins/'.Length)
        $stagedFile = [IO.Path]::GetFullPath((Join-Path $stagingPath $relative))
        if (!$stagedFile.StartsWith($stagingPath + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe archive entry.' }
        [IO.Directory]::CreateDirectory((Split-Path $stagedFile -Parent)) | Out-Null
        [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $stagedFile, $false)
        $files.Add([pscustomobject]@{Path=$relative; SHA256=(Get-FileHash -LiteralPath $stagedFile -Algorithm SHA256).Hash})
    }
    foreach ($required in @('DLSS/DLSS.uplugin','StreamlineNGXCommon/StreamlineNGXCommon.uplugin','DLSS/Source/DLSSBlueprint/Public/DLSSLibrary.h','DLSS/Source/ThirdParty/NGX/LICENSE.txt','DLSS/Binaries/ThirdParty/Win64/nvngx_dlss.dll','DLSS/Binaries/ThirdParty/Win64/nvngx_dlssd.dll')) {
        if (!(Test-Path -LiteralPath (Join-Path $stagingPath $required) -PathType Leaf)) { throw "Official package is incomplete: $required" }
    }
    foreach ($dll in @('nvngx_dlss.dll','nvngx_dlssd.dll')) {
        $signature = Get-AuthenticodeSignature -LiteralPath (Join-Path $stagingPath "DLSS/Binaries/ThirdParty/Win64/$dll")
        if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'O=NVIDIA Corporation') { throw "NVIDIA signature validation failed: $dll" }
    }
    # Validate all existing sources before moving either plugin; never overwrite local SDK changes.
    foreach ($file in $files) {
        $destination = Join-Path $vendorPath $file.Path
        if (Test-Path -LiteralPath $destination -PathType Leaf) {
            if ((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -ne $file.SHA256) { throw "Existing SDK file differs; preserved: $($file.Path)" }
        } elseif (Test-Path -LiteralPath (Join-Path $vendorPath ($file.Path.Split('/')[0]))) {
            throw "Existing SDK is incomplete; preserved: $($file.Path)"
        }
    }
    [IO.Directory]::CreateDirectory($vendorPath) | Out-Null
    foreach ($plugin in @('DLSS','StreamlineNGXCommon')) {
        $destination = Join-Path $vendorPath $plugin
        if (!(Test-Path -LiteralPath $destination)) { [IO.Directory]::Move((Join-Path $stagingPath $plugin), $destination) }
    }
    $licensePath = Join-Path $vendorPath 'NVIDIA RTX SDK EULA.pdf'
    if (!(Test-Path -LiteralPath $licensePath)) {
        [IO.Compression.ZipFileExtensions]::ExtractToFile($archive.GetEntry('Documentation/NVIDIA RTX SDK EULA.pdf'), $licensePath, $false)
    }
    [pscustomobject]@{Package=$packageName;PackageSHA256=$packageHash;Source=$packageUrl;Plugins=@('DLSS','StreamlineNGXCommon');Files=$files} |
        ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $vendorPath 'SDK-PROVENANCE.json') -Encoding utf8NoBOM
} finally { $archive.Dispose() }
Write-Output 'Prepared official UE5.8 DLSS4.5 SDK 8.9.0 (NGX310.9.16); source-build inputs and signed production DLLs only.'
Write-Output 'NVIDIA SDK files retain their NVIDIA license and must remain excluded from the MIT source repository.'
