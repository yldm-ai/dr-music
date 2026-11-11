# Original DR Music icon, drawn from the same waveform as the application UI.
Add-Type -AssemblyName System.Drawing
$iconDirectory = Join-Path $PSScriptRoot '../assets'
New-Item -ItemType Directory -Force $iconDirectory | Out-Null
$bitmap = [System.Drawing.Bitmap]::new(256, 256)
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$graphics.Clear([System.Drawing.Color]::Transparent)
$shape = [System.Drawing.Drawing2D.GraphicsPath]::new()
$shape.AddArc(8, 8, 76, 76, 180, 90)
$shape.AddArc(172, 8, 76, 76, 270, 90)
$shape.AddArc(172, 172, 76, 76, 0, 90)
$shape.AddArc(8, 172, 76, 76, 90, 90)
$shape.CloseFigure()
$brush = [System.Drawing.SolidBrush]::new([System.Drawing.ColorTranslator]::FromHtml('#F17D72'))
$graphics.FillPath($brush, $shape)
$pen = [System.Drawing.Pen]::new([System.Drawing.ColorTranslator]::FromHtml('#292022'), 14)
$pen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
$pen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
$waveHeights = @(35, 78, 125, 60, 98, 32)
for ($index = 0; $index -lt $waveHeights.Length; $index++) {
    $x = 63 + $index * 26
    $half = $waveHeights[$index] / 2
    $graphics.DrawLine($pen, [single]$x, [single](128 - $half), [single]$x, [single](128 + $half))
}
$pngPath = Join-Path $iconDirectory 'icon.png'
$bitmap.Save($pngPath, [System.Drawing.Imaging.ImageFormat]::Png)
$pngBytes = [System.IO.File]::ReadAllBytes($pngPath)
$iconStream = [System.IO.File]::Create((Join-Path $iconDirectory 'icon.ico'))
$writer = [System.IO.BinaryWriter]::new($iconStream)
$writer.Write([uint16]0); $writer.Write([uint16]1); $writer.Write([uint16]1)
$writer.Write([byte]0); $writer.Write([byte]0); $writer.Write([byte]0); $writer.Write([byte]0)
$writer.Write([uint16]1); $writer.Write([uint16]32)
$writer.Write([uint32]$pngBytes.Length); $writer.Write([uint32]22); $writer.Write($pngBytes)
$writer.Dispose(); $iconStream.Dispose()
$pen.Dispose(); $brush.Dispose(); $shape.Dispose(); $graphics.Dispose(); $bitmap.Dispose()
