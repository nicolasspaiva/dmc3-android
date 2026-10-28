# Gera os ícones do app a partir de uma imagem quadrada (PNG, JPG ou WebP): ic_launcher.png em cada densidade (ícone
# antigo, a imagem inteira) e ic_launcher_foreground.png (ícone adaptativo do Android 8+: a imagem ocupa o meio da
# camada de 108 dp, para a máscara do launcher não cortar o logotipo). Uso: .\make_icon.ps1 <imagem>
param([Parameter(Mandatory)][string]$Image)
Add-Type -AssemblyName PresentationCore, WindowsBase
$res = Join-Path $PSScriptRoot 'app\src\main\res'
$decoder = [System.Windows.Media.Imaging.BitmapDecoder]::Create([Uri](Resolve-Path $Image).Path, 'None', 'OnLoad')
$source = $decoder.Frames[0]

function Save-Png([System.Windows.Media.Imaging.BitmapSource]$bitmap, [string]$path)
{
    New-Item -ItemType Directory -Force (Split-Path $path) | Out-Null
    $encoder = New-Object System.Windows.Media.Imaging.PngBitmapEncoder
    $encoder.Frames.Add([System.Windows.Media.Imaging.BitmapFrame]::Create($bitmap))
    $stream = [IO.File]::Create($path)
    $encoder.Save($stream)
    $stream.Close()
}

# desenha a imagem com o lado 'inner' no centro de uma tela 'size' x 'size' transparente
function Render([int]$size, [int]$inner)
{
    $visual = New-Object System.Windows.Media.DrawingVisual
    $context = $visual.RenderOpen()
    $offset = ($size - $inner) / 2
    $context.DrawImage($source, (New-Object System.Windows.Rect($offset, $offset, $inner, $inner)))
    $context.Close()
    $target = New-Object System.Windows.Media.Imaging.RenderTargetBitmap($size, $size, 96, 96, [System.Windows.Media.PixelFormats]::Pbgra32)
    [System.Windows.Media.RenderOptions]::SetBitmapScalingMode($visual, 'HighQuality')
    $target.Render($visual)
    return $target
}

$densities = @{ 'mdpi' = 1.0; 'hdpi' = 1.5; 'xhdpi' = 2.0; 'xxhdpi' = 3.0; 'xxxhdpi' = 4.0 }
foreach ($d in $densities.Keys)
{
    $scale = $densities[$d]
    $legacy = [int](48 * $scale)
    Save-Png (Render $legacy $legacy) "$res\mipmap-$d\ic_launcher.png"
    $layer = [int](108 * $scale)
    Save-Png (Render $layer ([int](80 * $scale))) "$res\mipmap-$d\ic_launcher_foreground.png"
}
"icones gerados em $res"   # sem acento: o PowerShell 5.1 lê o arquivo sem BOM como ANSI
