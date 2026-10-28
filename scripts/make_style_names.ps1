# Gera runtime/style_names.h: os nomes dos seis estilos desenhados com uma fonte do Windows como máscaras de 8 bits
# (alfa), que o renderer pinta na cor de cada estilo no aviso de troca de estilo. O Android não tem as fontes do
# Windows, por isso as imagens vão prontas no código.
# Uso: .\make_style_names.ps1 [-Font 'Gabriola'] [-Preview arquivo.png]
param([string]$Font = 'Palatino Linotype', [string]$Weight = 'Bold', [string]$Style = 'Italic', [int]$Size = 64,
    [string]$Preview = '')
Add-Type -AssemblyName PresentationCore, WindowsBase, PresentationFramework
$names = 'Swordmaster', 'Gunslinger', 'Trickster', 'Royalguard', 'Quicksilver', 'Doppelganger'   # ordem do jogo
$typeface = New-Object System.Windows.Media.Typeface((New-Object System.Windows.Media.FontFamily($Font)),
    [System.Windows.FontStyles]::$Style, [System.Windows.FontWeights]::$Weight, [System.Windows.FontStretches]::Normal)

$images = @()
foreach ($name in $names)
{
    $text = New-Object System.Windows.Media.FormattedText($name, [Globalization.CultureInfo]::InvariantCulture,
        'LeftToRight', $typeface, $Size, [System.Windows.Media.Brushes]::White, 1.0)
    $w = [int][Math]::Ceiling($text.WidthIncludingTrailingWhitespace + $Size * 0.3)
    $h = [int][Math]::Ceiling($text.Height + 4)
    $visual = New-Object System.Windows.Media.DrawingVisual
    $context = $visual.RenderOpen()
    $context.DrawText($text, (New-Object System.Windows.Point(($Size * 0.1), 2)))
    $context.Close()
    $bitmap = New-Object System.Windows.Media.Imaging.RenderTargetBitmap($w, $h, 96, 96, [System.Windows.Media.PixelFormats]::Pbgra32)
    $bitmap.Render($visual)
    $pixels = New-Object byte[] ($w * $h * 4)
    $bitmap.CopyPixels($pixels, $w * 4, 0)
    $alpha = New-Object byte[] ($w * $h)
    for ($i = 0; $i -lt $alpha.Length; $i++) { $alpha[$i] = $pixels[$i * 4 + 3] }
    $images += [pscustomobject]@{ Name = $name; Width = $w; Height = $h; Alpha = $alpha; Bitmap = $bitmap }
}

if ($Preview)
{
    # prévia: os seis nomes empilhados sobre fundo escuro
    $pw = ($images | Measure-Object Width -Maximum).Maximum
    $ph = ($images | Measure-Object Height -Sum).Sum
    $visual = New-Object System.Windows.Media.DrawingVisual
    $context = $visual.RenderOpen()
    $context.DrawRectangle([System.Windows.Media.Brushes]::Black, $null, (New-Object System.Windows.Rect(0, 0, $pw, $ph)))
    $y = 0
    foreach ($image in $images) { $context.DrawImage($image.Bitmap, (New-Object System.Windows.Rect(0, $y, $image.Width, $image.Height))); $y += $image.Height }
    $context.Close()
    $bitmap = New-Object System.Windows.Media.Imaging.RenderTargetBitmap($pw, $ph, 96, 96, [System.Windows.Media.PixelFormats]::Pbgra32)
    $bitmap.Render($visual)
    $encoder = New-Object System.Windows.Media.Imaging.PngBitmapEncoder
    $encoder.Frames.Add([System.Windows.Media.Imaging.BitmapFrame]::Create($bitmap))
    $stream = [IO.File]::Create($Preview); $encoder.Save($stream); $stream.Close()
    return
}

$out = New-Object System.Text.StringBuilder
[void]$out.AppendLine("// Gerado por scripts/make_style_names.ps1 (fonte $Font $Weight $Style, $Size px): nomes dos estilos como máscaras")
[void]$out.AppendLine("// alfa de 8 bits, na ordem do jogo (Swordmaster, Gunslinger, Trickster, Royalguard, Quicksilver, Doppelganger).")
[void]$out.AppendLine('#pragma once')
[void]$out.AppendLine('#include <cstdint>')
[void]$out.AppendLine('namespace stylenames')
[void]$out.AppendLine('{')
[void]$out.AppendLine('    struct Image { uint32_t width, height; const uint8_t* alpha; };')
for ($k = 0; $k -lt $images.Count; $k++)
{
    $image = $images[$k]
    [void]$out.Append("    inline const uint8_t Alpha$k[] = {")
    for ($i = 0; $i -lt $image.Alpha.Length; $i++)
    {
        if ($i % 40 -eq 0) { [void]$out.Append("`n        ") }
        [void]$out.Append($image.Alpha[$i]).Append(',')
    }
    [void]$out.AppendLine("`n    };")
}
[void]$out.AppendLine('    inline const Image Images[6] = {')
for ($k = 0; $k -lt $images.Count; $k++) { [void]$out.AppendLine("        { $($images[$k].Width), $($images[$k].Height), Alpha$k },") }
[void]$out.AppendLine('    };')
[void]$out.AppendLine('}')
$path = Join-Path $PSScriptRoot '..\runtime\style_names.h'
[IO.File]::WriteAllText($path, $out.ToString(), (New-Object System.Text.UTF8Encoding($false)))
"gerado $path"
