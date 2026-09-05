# Copy the project to the Raspberry Pi over SSH. Run this from Windows after
# every edit:  .\sync.ps1
#
# models/ and data/ are deliberately not copied - they live only on the Pi.

$PiTarget  = if ($env:PI_TARGET) { $env:PI_TARGET } else { "admin@192.168.0.111" }
$RemoteDir = "/home/admin/face-recognition"
$LocalDir  = $PSScriptRoot

Write-Host "Syncing $LocalDir -> ${PiTarget}:$RemoteDir"
ssh $PiTarget "mkdir -p $RemoteDir"

foreach ($item in @("src", "tools", "tests", "run.sh", "download_models.sh")) {
    $path = Join-Path $LocalDir $item
    if (Test-Path $path) {
        scp -r $path "${PiTarget}:$RemoteDir/"
    }
}

ssh $PiTarget "chmod +x $RemoteDir/run.sh $RemoteDir/download_models.sh 2>/dev/null; true"
Write-Host "Done."
