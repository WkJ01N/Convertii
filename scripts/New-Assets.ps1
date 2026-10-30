param([string]$OutputDirectory = (Join-Path (Split-Path $PSScriptRoot -Parent) 'artifacts\assets'))
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null

# The mark is a white double arrow (convert) on the brand orange plate. It is
# drawn per size instead of being scaled, so the 16 px entry keeps solid stems.
function New-Mark([int]$Size) {
    $bitmap = New-Object Drawing.Bitmap $Size,$Size,([Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::AntiAlias
        $graphics.PixelOffsetMode = [Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        $graphics.Clear([Drawing.Color]::Transparent)
        $pad = [single]([Math]::Max(1.0, [Math]::Round($Size * .06)))
        $radius = [single]([Math]::Max(2.0, $Size * .22))
        $width = [single]($Size - 2 * $pad)
        $diameter = [single]($radius * 2)
        $path = New-Object Drawing.Drawing2D.GraphicsPath
        $path.AddArc($pad, $pad, $diameter, $diameter, 180, 90)
        $path.AddArc($pad + $width - $diameter, $pad, $diameter, $diameter, 270, 90)
        $path.AddArc($pad + $width - $diameter, $pad + $width - $diameter, $diameter, $diameter, 0, 90)
        $path.AddArc($pad, $pad + $width - $diameter, $diameter, $diameter, 90, 90)
        $path.CloseFigure()
        $brush = New-Object Drawing.SolidBrush ([Drawing.Color]::FromArgb(255, 200, 82, 45))
        try { $graphics.FillPath($brush, $path) } finally { $brush.Dispose(); $path.Dispose() }

        $stroke = [single]([Math]::Max(1.6, $Size * .115))
        # Below 24 px the default stems are too fat for the two arrows to stay
        # apart, so the small entries get thinner strokes and a wider gap.
        $small = $Size -le 24
        if ($small) { $stroke = [single]([Math]::Max(1.5, $Size * .105)) }
        $pen = New-Object Drawing.Pen ([Drawing.Color]::White), $stroke
        try {
            $pen.StartCap = [Drawing.Drawing2D.LineCap]::Round
            $pen.EndCap = [Drawing.Drawing2D.LineCap]::Round
            $pen.LineJoin = [Drawing.Drawing2D.LineJoin]::Round
            $margin = [single]($Size * .245)
            $head = [single]($Size * .135)
            $wing = [single]($Size * .105)
            $top = [single]($Size * .375)
            $bottom = [single]($Size * .625)
            if ($small) {
                $margin = [single]($Size * .220)
                $head = [single]($Size * .150)
                $wing = [single]($Size * .100)
                $top = [single]($Size * .355)
                $bottom = [single]($Size * .645)
            }
            # upper arrow points right
            $graphics.DrawLine($pen, $margin, $top, $Size - $margin - $head, $top)
            $graphics.DrawLine($pen, $Size - $margin, $top, $Size - $margin - $head, $top - $wing)
            $graphics.DrawLine($pen, $Size - $margin, $top, $Size - $margin - $head, $top + $wing)
            # lower arrow points left
            $graphics.DrawLine($pen, $Size - $margin, $bottom, $margin + $head, $bottom)
            $graphics.DrawLine($pen, $margin, $bottom, $margin + $head, $bottom - $wing)
            $graphics.DrawLine($pen, $margin, $bottom, $margin + $head, $bottom + $wing)
        } finally { $pen.Dispose() }
        return $bitmap
    } finally { $graphics.Dispose() }
}

# A bitmap ICO entry: BITMAPINFOHEADER, bottom-up BGRA pixels, then the AND mask.
function Get-IconBitmap([System.Drawing.Bitmap]$Bitmap) {
    $size = $Bitmap.Width
    $bounds = New-Object Drawing.Rectangle 0,0,$size,$size
    $locked = $Bitmap.LockBits($bounds, [Drawing.Imaging.ImageLockMode]::ReadOnly, [Drawing.Imaging.PixelFormat]::Format32bppArgb)
    try {
        $stride = $locked.Stride
        $pixels = New-Object byte[] ($stride * $size)
        [Runtime.InteropServices.Marshal]::Copy($locked.Scan0, $pixels, 0, $pixels.Length)
    } finally { $Bitmap.UnlockBits($locked) }
    $maskRow = [int]([Math]::Ceiling($size / 32.0) * 4)
    $stream = New-Object IO.MemoryStream
    $writer = New-Object IO.BinaryWriter $stream
    try {
        $imageBytes = $size * $size * 4 + $maskRow * $size
        $writer.Write([uint32]40); $writer.Write([int32]$size); $writer.Write([int32]($size * 2))
        $writer.Write([uint16]1); $writer.Write([uint16]32); $writer.Write([uint32]0)
        $writer.Write([uint32]$imageBytes); $writer.Write([int32]0); $writer.Write([int32]0)
        $writer.Write([uint32]0); $writer.Write([uint32]0)
        for ($row = $size - 1; $row -ge 0; --$row) { $writer.Write($pixels, $row * $stride, $size * 4) }
        $writer.Write((New-Object byte[] ($maskRow * $size)))
        $writer.Flush()
        # The unary comma stops PowerShell from unrolling the byte array into
        # separate pipeline items, which would lose the entry payload.
        return ,$stream.ToArray()
    } finally { $writer.Dispose(); $stream.Dispose() }
}

function Get-IconPng([System.Drawing.Bitmap]$Bitmap) {
    $stream = New-Object IO.MemoryStream
    try { $Bitmap.Save($stream, [Drawing.Imaging.ImageFormat]::Png); return ,$stream.ToArray() }
    finally { $stream.Dispose() }
}

# Windows picks the closest entry for 16/20/24/32/40/48/64 and uses the 256 PNG
# for the extra large shell views. Only 256 is stored as PNG, the rest as DIB.
$entries = @()
foreach ($size in @(16, 20, 24, 32, 40, 48, 64)) {
    $bitmap = New-Mark $size
    try { $entries += ,@($size, (Get-IconBitmap $bitmap)) } finally { $bitmap.Dispose() }
}
$large = New-Mark 256
try { $entries += ,@(256, (Get-IconPng $large)) } finally { $large.Dispose() }

$ico = Join-Path $OutputDirectory 'Convertii.ico'
$stream = [IO.File]::Create($ico)
$writer = New-Object IO.BinaryWriter $stream
try {
    $writer.Write([uint16]0); $writer.Write([uint16]1); $writer.Write([uint16]$entries.Count)
    $offset = 6 + 16 * $entries.Count
    foreach ($entry in $entries) {
        $size = [int]$entry[0]; $bytes = $entry[1]
        $dimension = [byte]([Math]::Min(255, $size)); if ($size -ge 256) { $dimension = [byte]0 }
        $writer.Write($dimension); $writer.Write($dimension); $writer.Write([byte]0); $writer.Write([byte]0)
        $writer.Write([uint16]1); $writer.Write([uint16]32)
        $writer.Write([uint32]$bytes.Length); $writer.Write([uint32]$offset)
        $offset += $bytes.Length
    }
    foreach ($entry in $entries) { $writer.Write($entry[1]) }
} finally { $writer.Dispose(); $stream.Dispose() }

# Store and tile artwork for the identity package, same mark at the sizes the
# manifest references.
foreach ($spec in @(@(44, 'Logo44.png'), @(50, 'StoreLogo.png'), @(150, 'Logo150.png'), @(256, 'Icon256.png'))) {
    $bitmap = New-Mark $spec[0]
    try { $bitmap.Save((Join-Path $OutputDirectory $spec[1]), [Drawing.Imaging.ImageFormat]::Png) } finally { $bitmap.Dispose() }
}
Write-Host ("Wrote {0} and {1} images to {2}" -f 'Convertii.ico', '4', $OutputDirectory)
