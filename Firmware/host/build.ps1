param([string]$Csc = "$env:WINDIR/Microsoft.NET/Framework64/v4.0.30319/csc.exe")
$ErrorActionPreference='Stop'
if (-not (Test-Path -LiteralPath $Csc)) { throw 'Install .NET Framework 4.8 or pass -Csc with a C# compiler.' }
$out=Join-Path $PSScriptRoot 'build'
New-Item -ItemType Directory -Force $out | Out-Null
$repo=Split-Path (Split-Path $PSScriptRoot)
$sources=@('Device.cs','MainForm.cs','Program.cs','McpServer.cs','AssemblyInfo.cs') | ForEach-Object { Join-Path $PSScriptRoot $_ }
$refs=@('/r:System.dll','/r:System.Core.dll','/r:System.Drawing.dll','/r:System.Windows.Forms.dll','/r:System.Web.Extensions.dll')
$common=@('/nologo','/optimize+','/platform:anycpu','/utf8output',"/win32icon:$repo\Images\coral-logo.ico", "/resource:$repo\Images\coral-logo.png,CoralWave.Logo.png", "/resource:$repo\Images\coral-logo.ico,CoralWave.Icon.ico")+$refs+$sources
& $Csc /target:winexe "/out:$out\Coral-Wave-Control.exe" @common
if ($LASTEXITCODE) { throw 'GUI build failed' }
& $Csc /target:exe "/out:$out\Coral-Wave-CLI.exe" @common
if ($LASTEXITCODE) { throw 'CLI build failed' }
Write-Output "Built $out/Coral-Wave-Control.exe and Coral-Wave-CLI.exe"
