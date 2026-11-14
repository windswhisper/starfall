param([Parameter(Mandatory)][int]$Port,[Parameter(Mandatory)][string]$Directory)
$ErrorActionPreference='Stop'
Add-Type -Path (Join-Path $PSScriptRoot 'UdpRelay.cs')
[StarfallUdpRelay]::Run($Port,$Directory)
