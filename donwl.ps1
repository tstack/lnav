# Define variables
$RepoOwner = "tstack"  # Change this if you're using your fork
$RepoName = "lnav"
$ArtifactName = "lnav-master-windows-amd64.zip"  # Update if needed
$InstallPath = "C:\Program Files\lnav"

# Get the latest run with artifacts
$LatestRunUrl = "https://api.github.com/repos/$RepoOwner/$RepoName/actions/runs"
$LatestRun = Invoke-RestMethod -Uri $LatestRunUrl -Headers @{ "Accept" = "application/vnd.github.v3+json" }
$RunId = $LatestRun.workflow_runs[0].id  # Get latest run ID

# Get artifacts from the latest run
$ArtifactsUrl = "https://api.github.com/repos/$RepoOwner/$RepoName/actions/runs/$RunId/artifacts"
$Artifacts = Invoke-RestMethod -Uri $ArtifactsUrl -Headers @{ "Accept" = "application/vnd.github.v3+json" }

# Find the Windows artifact
$Artifact = $Artifacts.artifacts | Where-Object { $_.name -eq $ArtifactName }
if (-not $Artifact) {
    Write-Host "❌ Windows artifact not found. Exiting..."
    exit 1
}

# Download the artifact
$DownloadUrl = $Artifact.archive_download_url
$ZipPath = "$env:TEMP\lnav-windows.zip"

# GitHub API requires authentication to download workflow artifacts
$GitHubToken = Read-Host "Enter GitHub Token (with repo read permissions)" -AsSecureString
$BSTR = [System.Runtime.InteropServices.Marshal]::SecureStringToBSTR($GitHubToken)
$PlainToken = [System.Runtime.InteropServices.Marshal]::PtrToStringAuto($BSTR)

Invoke-WebRequest -Uri $DownloadUrl -Headers @{ "Authorization" = "token $PlainToken" } -OutFile $ZipPath

# Extract and install
$ExtractPath = "$env:TEMP\lnav"
Expand-Archive -Path $ZipPath -DestinationPath $ExtractPath -Force

# Move to install location
if (Test-Path $InstallPath) {
    Remove-Item -Recurse -Force $InstallPath
}
Move-Item -Path "$ExtractPath\*" -Destination $InstallPath -Force

# Add to PATH
$SystemPath = [System.Environment]::GetEnvironmentVariable("Path", [System.EnvironmentVariableTarget]::Machine)
if ($SystemPath -notlike "*$InstallPath*") {
    [System.Environment]::SetEnvironmentVariable("Path", "$SystemPath;$InstallPath", [System.EnvironmentVariableTarget]::Machine)
}

Write-Host "✅ lnav installed successfully! Run 'lnav' in a new terminal."
