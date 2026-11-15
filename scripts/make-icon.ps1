# Regenerate the PNG and Windows icon from the original SVG. Requires ImageMagick on PATH.
$ErrorActionPreference = 'Stop'
$iconDirectory = Join-Path $PSScriptRoot '../assets'
$svgPath = Join-Path $iconDirectory 'icon.svg'
$pngPath = Join-Path $iconDirectory 'icon.png'
$icoPath = Join-Path $iconDirectory 'icon.ico'
$magick = Get-Command magick -ErrorAction Stop

& $magick.Source -background none -density 192 $svgPath -resize 1024x1024 $pngPath
if ($LASTEXITCODE -ne 0) { throw 'ImageMagick failed to create icon.png' }

& (Join-Path $PSScriptRoot 'convert-png-to-ico.ps1') $svgPath $icoPath -Force
