$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

# Package the existing portrait without changing its source image.
# Native DIB frames avoid PNG-backed DrawIcon corruption in .NET Framework.
$taskMaster = [System.Drawing.Bitmap]::new((Join-Path $PSScriptRoot 'launcher-icon.png'))
try {
    $taskFrames = @()
    foreach ($size in @(16, 20, 24, 32, 40, 48, 64, 128, 256)) {
        $bitmap = [System.Drawing.Bitmap]::new($size, $size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
        $stream = [System.IO.MemoryStream]::new()
        try {
            $graphics.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
            $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
            $graphics.DrawImage($taskMaster, [System.Drawing.Rectangle]::new(0, 0, $size, $size))
            # Use native 32-bit DIB frames: .NET Framework DrawIcon can fail
            # when resizing an icon whose selected frame is PNG-compressed.
            $frameWriter = [System.IO.BinaryWriter]::new($stream)
            try {
                $maskStride = [int]([Math]::Floor(($size + 31) / 32) * 4)
                $frameWriter.Write([uint32]40)
                $frameWriter.Write([int32]$size); $frameWriter.Write([int32]($size * 2))
                $frameWriter.Write([uint16]1); $frameWriter.Write([uint16]32)
                $frameWriter.Write([uint32]0); $frameWriter.Write([uint32]($size * $size * 4))
                foreach ($unused in 1..4) { $frameWriter.Write([uint32]0) }
                $mask = [byte[]]::new($maskStride * $size)
                for ($y = $size - 1; $y -ge 0; $y--) {
                    for ($x = 0; $x -lt $size; $x++) {
                        $pixel = $bitmap.GetPixel($x, $y)
                        $frameWriter.Write([byte]$pixel.B); $frameWriter.Write([byte]$pixel.G)
                        $frameWriter.Write([byte]$pixel.R); $frameWriter.Write([byte]$pixel.A)
                        if ($pixel.A -lt 128) {
                            $index = ($size - 1 - $y) * $maskStride + [int][Math]::Floor($x / 8)
                            $mask[$index] = $mask[$index] -bor (0x80 -shr ($x % 8))
                        }
                    }
                }
                $frameWriter.Write($mask)
                $frameWriter.Flush()
                $frameBytes = $stream.ToArray()
            } finally { $frameWriter.Dispose() }
            $taskFrames += [pscustomobject]@{ Size = $size; Bytes = $frameBytes }
        } finally { $stream.Dispose(); $graphics.Dispose(); $bitmap.Dispose() }
    }

    $taskFile = [System.IO.File]::Create((Join-Path $PSScriptRoot 'launcher.ico'))
    $taskWriter = [System.IO.BinaryWriter]::new($taskFile)
    try {
        $taskWriter.Write([uint16]0); $taskWriter.Write([uint16]1); $taskWriter.Write([uint16]$taskFrames.Count)
        $offset = 6 + 16 * $taskFrames.Count
        foreach ($frame in $taskFrames) {
            $dimension = if ($frame.Size -eq 256) { 0 } else { $frame.Size }
            $taskWriter.Write([byte]$dimension); $taskWriter.Write([byte]$dimension)
            $taskWriter.Write([byte]0); $taskWriter.Write([byte]0)
            $taskWriter.Write([uint16]1); $taskWriter.Write([uint16]32)
            $taskWriter.Write([uint32]$frame.Bytes.Length); $taskWriter.Write([uint32]$offset)
            $offset += $frame.Bytes.Length
        }
        foreach ($frame in $taskFrames) { $taskWriter.Write([byte[]]$frame.Bytes) }
    } finally { $taskWriter.Dispose(); $taskFile.Dispose() }
} finally { $taskMaster.Dispose() }
Write-Host '已将原图封装为 16–256 像素的透明 ICO。'