param([string]$OutputDirectory = (Join-Path (Split-Path $PSScriptRoot -Parent) 'artifacts\assets'))
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
foreach ($size in @(44, 50, 150, 256)) {
    $bitmap = New-Object Drawing.Bitmap $size,$size
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::AntiAlias
        $graphics.Clear([Drawing.Color]::FromArgb(200,82,45))
        $pen = New-Object Drawing.Pen ([Drawing.Color]::White),([single]($size * .09))
        $pen.StartCap = $pen.EndCap = [Drawing.Drawing2D.LineCap]::Round
        try {
            $graphics.DrawArc($pen, [single]($size*.22), [single]($size*.22), [single]($size*.56), [single]($size*.56), 45, 280)
            $graphics.DrawLine($pen, [single]($size*.74), [single]($size*.36), [single]($size*.76), [single]($size*.16))
            $graphics.DrawLine($pen, [single]($size*.76), [single]($size*.16), [single]($size*.56), [single]($size*.18))
        } finally { $pen.Dispose() }
        $name = switch ($size) { 44 {'Logo44.png'} 50 {'StoreLogo.png'} 150 {'Logo150.png'} 256 {'Icon256.png'} }
        $bitmap.Save((Join-Path $OutputDirectory $name), [Drawing.Imaging.ImageFormat]::Png)
    } finally { $graphics.Dispose(); $bitmap.Dispose() }
}
# ICO containing a PNG-compressed 256x256 image; no unmanaged icon handle is needed.
$png = [IO.File]::ReadAllBytes((Join-Path $OutputDirectory 'Icon256.png'))
$stream = [IO.File]::Create((Join-Path $OutputDirectory 'Convertii.ico'))
$writer = New-Object IO.BinaryWriter $stream
try {
    $writer.Write([uint16]0); $writer.Write([uint16]1); $writer.Write([uint16]1)
    $writer.Write([byte]0); $writer.Write([byte]0); $writer.Write([byte]0); $writer.Write([byte]0)
    $writer.Write([uint16]1); $writer.Write([uint16]32); $writer.Write([uint32]$png.Length); $writer.Write([uint32]22); $writer.Write($png)
} finally { $writer.Dispose() }
