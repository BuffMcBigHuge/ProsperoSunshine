param([switch]$RebuildImage)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$image = 'prosperosunshine-sdk:v0.45'
docker image inspect $image 2>$null | Out-Null
if ($RebuildImage -or $LASTEXITCODE -ne 0) {
    docker build -t $image -f "$PSScriptRoot/Dockerfile" $projectRoot
    if ($LASTEXITCODE -ne 0) { throw 'SDK image build failed' }
}
docker run --rm --mount "type=bind,source=$projectRoot,target=/repo" $image bash scripts/build.sh
if ($LASTEXITCODE -ne 0) { throw 'PS5 build failed' }
