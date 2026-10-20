param(
    [Parameter(Mandatory = $true)]
    [string]$ClosedLoopImage,

    [Parameter(Mandatory = $true)]
    [string]$ConfigurationImage,

    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'

function Get-Rgb([string]$HexColor) {
    $hex = $HexColor.TrimStart('#')
    $red = [Convert]::ToInt32($hex.Substring(0, 2), 16)
    $green = [Convert]::ToInt32($hex.Substring(2, 2), 16)
    $blue = [Convert]::ToInt32($hex.Substring(4, 2), 16)
    return $red + (256 * $green) + (65536 * $blue)
}

function Add-Rectangle {
    param(
        $Slide,
        [double]$Left,
        [double]$Top,
        [double]$Width,
        [double]$Height,
        [string]$FillColor,
        [string]$LineColor = $FillColor,
        [double]$LineWeight = 0.8,
        [int]$ShapeType = 1
    )

    $shape = $Slide.Shapes.AddShape($ShapeType, $Left, $Top, $Width, $Height)
    $shape.Fill.Solid()
    $shape.Fill.ForeColor.RGB = Get-Rgb $FillColor
    $shape.Line.ForeColor.RGB = Get-Rgb $LineColor
    $shape.Line.Weight = $LineWeight
    return $shape
}

function Add-Text {
    param(
        $Slide,
        [string]$Text,
        [double]$Left,
        [double]$Top,
        [double]$Width,
        [double]$Height,
        [double]$FontSize = 16,
        [string]$FontColor = '#263740',
        [bool]$Bold = $false,
        [int]$Alignment = 1,
        [int]$VerticalAnchor = 3
    )

    $shape = $Slide.Shapes.AddTextbox(1, [single]$Left, [single]$Top, [single]$Width, [single]$Height)
    $shape.Fill.Visible = 0
    $shape.Line.Visible = 0
    $shape.TextFrame2.MarginLeft = 2
    $shape.TextFrame2.MarginRight = 2
    $shape.TextFrame2.MarginTop = 1
    $shape.TextFrame2.MarginBottom = 1
    $shape.TextFrame2.VerticalAnchor = $VerticalAnchor
    $shape.TextFrame2.WordWrap = -1
    $range = $shape.TextFrame2.TextRange
    $range.Text = $Text
    $range.Font.Name = 'Microsoft YaHei'
    $range.Font.NameFarEast = 'Microsoft YaHei'
    $range.Font.Size = $FontSize
    $range.Font.Bold = $(if ($Bold) { -1 } else { 0 })
    $range.Font.Fill.ForeColor.RGB = Get-Rgb $FontColor
    $range.ParagraphFormat.Alignment = $Alignment
    return $shape
}

function Add-Card {
    param(
        $Slide,
        [string]$Number,
        [string]$Title,
        [string]$Detail,
        [double]$Left,
        [double]$Top,
        [double]$Width,
        [double]$Height,
        [string]$AccentColor = '#1782A8'
    )

    $null = Add-Rectangle $Slide $Left $Top $Width $Height '#FFFFFF' '#AEBBC2' 0.8 5
    $null = Add-Rectangle $Slide ($Left + 8) ($Top + 8) 24 24 $AccentColor $AccentColor 0 9
    $null = Add-Text -Slide $Slide -Text $Number -Left ($Left + 8) -Top ($Top + 8) -Width 24 -Height 24 -FontSize 11 -FontColor '#FFFFFF' -Bold $true -Alignment 2 -VerticalAnchor 3
    $null = Add-Text -Slide $Slide -Text $Title -Left ($Left + 40) -Top ($Top + 6) -Width ($Width - 46) -Height 22 -FontSize 12 -FontColor '#233640' -Bold $true -Alignment 1 -VerticalAnchor 3
    $null = Add-Text -Slide $Slide -Text $Detail -Left ($Left + 40) -Top ($Top + 27) -Width ($Width - 46) -Height ($Height - 31) -FontSize 9.5 -FontColor '#61727B' -Bold $false -Alignment 1 -VerticalAnchor 1
}

function Add-StatusPill {
    param(
        $Slide,
        [string]$Text,
        [double]$Left,
        [double]$Top,
        [double]$Width,
        [string]$FillColor,
        [string]$TextColor = '#FFFFFF'
    )

    $null = Add-Rectangle $Slide $Left $Top $Width 25 $FillColor $FillColor 0 5
    $null = Add-Text -Slide $Slide -Text $Text -Left $Left -Top $Top -Width $Width -Height 25 -FontSize 10 -FontColor $TextColor -Bold $true -Alignment 2 -VerticalAnchor 3
}

function Add-PictureFit {
    param(
        $Slide,
        [string]$Path,
        [double]$Left,
        [double]$Top,
        [double]$Width,
        [double]$Height
    )

    $picture = $Slide.Shapes.AddPicture($Path, 0, -1, 0, 0, -1, -1)
    $picture.LockAspectRatio = -1
    $scale = [Math]::Min($Width / $picture.Width, $Height / $picture.Height)
    $picture.Width = $picture.Width * $scale
    $picture.Left = $Left + (($Width - $picture.Width) / 2)
    $picture.Top = $Top + (($Height - $picture.Height) / 2)
    return $picture
}

function Get-FreeOutputPath([string]$Directory, [string]$BaseName, [string]$Extension) {
    $candidate = Join-Path $Directory ($BaseName + $Extension)
    $index = 2
    while (Test-Path -LiteralPath $candidate) {
        $candidate = Join-Path $Directory ("{0}_v{1}{2}" -f $BaseName, $index, $Extension)
        $index++
    }
    return $candidate
}

if (-not (Test-Path -LiteralPath $ClosedLoopImage)) {
    throw "Closed-loop screenshot not found: $ClosedLoopImage"
}
if (-not (Test-Path -LiteralPath $ConfigurationImage)) {
    throw "Configuration screenshot not found: $ConfigurationImage"
}
if (-not (Test-Path -LiteralPath $OutputDirectory)) {
    $null = New-Item -ItemType Directory -Path $OutputDirectory
}

$outputPath = Get-FreeOutputPath $OutputDirectory 'VisionSoftware_阶段成果汇报' '.pptx'
$previewDirectory = Join-Path $OutputDirectory 'VisionSoftware_PPT预览'
$previewIndex = 2
while (Test-Path -LiteralPath $previewDirectory) {
    $previewDirectory = Join-Path $OutputDirectory ("VisionSoftware_PPT预览_v{0}" -f $previewIndex)
    $previewIndex++
}
$null = New-Item -ItemType Directory -Path $previewDirectory

$powerPoint = $null
$presentation = $null

try {
    $powerPoint = New-Object -ComObject PowerPoint.Application
    $presentation = $powerPoint.Presentations.Add()
    $presentation.PageSetup.SlideWidth = 960
    $presentation.PageSetup.SlideHeight = 540

    # Slide 1: closed-loop proof.
    $slide1 = $presentation.Slides.Add(1, 12)
    $slide1.FollowMasterBackground = 0
    $slide1.Background.Fill.Solid()
    $slide1.Background.Fill.ForeColor.RGB = Get-Rgb '#EEF2F3'

    $null = Add-Rectangle $slide1 0 0 960 62 '#263740' '#263740' 0
    $null = Add-Rectangle $slide1 0 60 960 3 '#1E91B6' '#1E91B6' 0
    $null = Add-Text -Slide $slide1 -Text '抓边定位控制闭环已打通' -Left 26 -Top 8 -Width 560 -Height 34 -FontSize 25 -FontColor '#FFFFFF' -Bold $true -Alignment 1 -VerticalAnchor 3
    $null = Add-Text -Slide $slide1 -Text 'TCP 指令驱动  ·  7 图循环采集  ·  Edge 定位  ·  结果回传' -Left 28 -Top 38 -Width 620 -Height 18 -FontSize 10.5 -FontColor '#B7DCE8' -Bold $false -Alignment 1 -VerticalAnchor 3
    $null = Add-StatusPill -Slide $slide1 -Text 'CLOSED LOOP · PASS' -Left 755 -Top 18 -Width 176 -FillColor '#168B49'

    $null = Add-Rectangle $slide1 22 77 660 426 '#FFFFFF' '#95A5AD' 1
    $picture1 = $slide1.Shapes.AddPicture($ClosedLoopImage, 0, -1, 25, 80, -1, -1)
    $picture1.LockAspectRatio = -1
    $picture1.Width = 654
    $null = Add-Rectangle $slide1 22 503 660 37 '#EEF2F3' '#EEF2F3' 0
    $null = Add-Rectangle $slide1 22 77 660 426 '#FFFFFF' '#82949D' 1
    $slide1.Shapes.Item($slide1.Shapes.Count).Fill.Transparency = 1

    $null = Add-Text -Slide $slide1 -Text '一次请求的关键节点' -Left 706 -Top 78 -Width 224 -Height 28 -FontSize 15 -FontColor '#233640' -Bold $true -Alignment 1 -VerticalAnchor 3
    Add-Card -Slide $slide1 -Number '1' -Title 'TCP 请求进入' -Detail 'Locate + RequestId' -Left 706 -Top 112 -Width 224 -Height 54 -AccentColor '#1E91B6'
    $null = Add-Text -Slide $slide1 -Text '↓' -Left 796 -Top 166 -Width 40 -Height 18 -FontSize 14 -FontColor '#819199' -Bold $true -Alignment 2 -VerticalAnchor 3
    Add-Card -Slide $slide1 -Number '2' -Title '任务准入' -Detail 'Ready 校验并投递视觉线程' -Left 706 -Top 184 -Width 224 -Height 54 -AccentColor '#1E91B6'
    $null = Add-Text -Slide $slide1 -Text '↓' -Left 796 -Top 238 -Width 40 -Height 18 -FontSize 14 -FontColor '#819199' -Bold $true -Alignment 2 -VerticalAnchor 3
    Add-Card -Slide $slide1 -Number '3' -Title '图像与算法' -Detail '离线图像模拟采集，执行 Edge' -Left 706 -Top 256 -Width 224 -Height 54 -AccentColor '#1E91B6'
    $null = Add-Text -Slide $slide1 -Text '↓' -Left 796 -Top 310 -Width 40 -Height 18 -FontSize 14 -FontColor '#819199' -Bold $true -Alignment 2 -VerticalAnchor 3
    Add-Card -Slide $slide1 -Number '4' -Title '定位结果形成' -Detail 'X / Y / R + OK / NG' -Left 706 -Top 328 -Width 224 -Height 54 -AccentColor '#168B49'
    $null = Add-Text -Slide $slide1 -Text '↓' -Left 796 -Top 382 -Width 40 -Height 18 -FontSize 14 -FontColor '#819199' -Bold $true -Alignment 2 -VerticalAnchor 3
    Add-Card -Slide $slide1 -Number '5' -Title 'TCP 结果回复' -Detail '同一 RequestId 完成闭环关联' -Left 706 -Top 400 -Width 224 -Height 54 -AccentColor '#168B49'

    $null = Add-StatusPill -Slide $slide1 -Text 'TCP 链路已打通' -Left 706 -Top 468 -Width 108 -FillColor '#263740'
    $null = Add-StatusPill -Slide $slide1 -Text '7 / 7 循环完成' -Left 821 -Top 468 -Width 109 -FillColor '#168B49'
    $null = Add-Text -Slide $slide1 -Text '01' -Left 918 -Top 511 -Width 22 -Height 16 -FontSize 9 -FontColor '#7B8A92' -Bold $true -Alignment 2 -VerticalAnchor 3

    # Slide 2: industrial UI and phase boundary.
    $slide2 = $presentation.Slides.Add(2, 12)
    $slide2.FollowMasterBackground = 0
    $slide2.Background.Fill.Solid()
    $slide2.Background.Fill.ForeColor.RGB = Get-Rgb '#EEF2F3'

    $null = Add-Rectangle $slide2 0 0 960 62 '#263740' '#263740' 0
    $null = Add-Rectangle $slide2 0 60 960 3 '#1E91B6' '#1E91B6' 0
    $null = Add-Text -Slide $slide2 -Text '工业视觉软件界面与阶段成果' -Left 26 -Top 8 -Width 600 -Height 34 -FontSize 25 -FontColor '#FFFFFF' -Bold $true -Alignment 1 -VerticalAnchor 3
    $null = Add-Text -Slide $slide2 -Text '配置调试与运行监控分离，Edge 闭环优先落地，MarkPoint 按既定架构扩展' -Left 28 -Top 38 -Width 690 -Height 18 -FontSize 10.5 -FontColor '#B7DCE8' -Bold $false -Alignment 1 -VerticalAnchor 3
    $null = Add-StatusPill -Slide $slide2 -Text 'EDGE · 已完成' -Left 788 -Top 18 -Width 142 -FillColor '#168B49'

    $null = Add-Rectangle $slide2 22 78 662 411 '#FFFFFF' '#82949D' 1
    $null = Add-PictureFit $slide2 $ConfigurationImage 25 81 656 405
    $null = Add-Rectangle $slide2 704 78 226 411 '#FFFFFF' '#A6B2B8' 0.9 5
    $null = Add-Text -Slide $slide2 -Text '当前成果' -Left 720 -Top 91 -Width 180 -Height 25 -FontSize 16 -FontColor '#233640' -Bold $true -Alignment 1 -VerticalAnchor 3
    $null = Add-StatusPill -Slide $slide2 -Text '已完成' -Left 720 -Top 126 -Width 66 -FillColor '#168B49'
    $null = Add-Text -Slide $slide2 -Text 'Edge 抓边算法与配置' -Left 796 -Top 126 -Width 120 -Height 25 -FontSize 11 -FontColor '#263740' -Bold $true -Alignment 1 -VerticalAnchor 3
    $null = Add-StatusPill -Slide $slide2 -Text '已完成' -Left 720 -Top 158 -Width 66 -FillColor '#168B49'
    $null = Add-Text -Slide $slide2 -Text 'TCP 请求与结果回传' -Left 796 -Top 158 -Width 120 -Height 25 -FontSize 11 -FontColor '#263740' -Bold $true -Alignment 1 -VerticalAnchor 3
    $null = Add-StatusPill -Slide $slide2 -Text '已完成' -Left 720 -Top 190 -Width 66 -FillColor '#168B49'
    $null = Add-Text -Slide $slide2 -Text '7 图循环定位验证' -Left 796 -Top 190 -Width 120 -Height 25 -FontSize 11 -FontColor '#263740' -Bold $true -Alignment 1 -VerticalAnchor 3

    $null = Add-Rectangle $slide2 720 229 194 1 '#CBD3D7' '#CBD3D7' 0
    $null = Add-Text -Slide $slide2 -Text '界面能力' -Left 720 -Top 242 -Width 180 -Height 24 -FontSize 15 -FontColor '#233640' -Bold $true -Alignment 1 -VerticalAnchor 3
    $null = Add-Text -Slide $slide2 -Text "• 左侧分组配置导航`r`n• 中央单图 / 四图工作区`r`n• 定位结果与通信状态`r`n• 独立自动运行监控窗口" -Left 720 -Top 271 -Width 188 -Height 88 -FontSize 10.5 -FontColor '#52646D' -Bold $false -Alignment 1 -VerticalAnchor 1

    $null = Add-Rectangle $slide2 720 366 194 1 '#CBD3D7' '#CBD3D7' 0
    $null = Add-Text -Slide $slide2 -Text '下一阶段' -Left 720 -Top 378 -Width 180 -Height 24 -FontSize 15 -FontColor '#233640' -Bold $true -Alignment 1 -VerticalAnchor 3
    $null = Add-StatusPill -Slide $slide2 -Text '规划中' -Left 720 -Top 414 -Width 66 -FillColor '#B47A19'
    $null = Add-Text -Slide $slide2 -Text 'MarkPoint 四相机算法' -Left 796 -Top 414 -Width 120 -Height 25 -FontSize 10.5 -FontColor '#263740' -Bold $true -Alignment 1 -VerticalAnchor 3
    $null = Add-StatusPill -Slide $slide2 -Text '规划中' -Left 720 -Top 446 -Width 66 -FillColor '#B47A19'
    $null = Add-Text -Slide $slide2 -Text '真实相机 / 光源接入' -Left 796 -Top 446 -Width 120 -Height 25 -FontSize 10.5 -FontColor '#263740' -Bold $true -Alignment 1 -VerticalAnchor 3

    $null = Add-Rectangle $slide2 22 504 908 25 '#DCE4E7' '#B2C0C6' 0.7 5
    $null = Add-Text -Slide $slide2 -Text '配置模式负责参数维护与调试；运行模式负责自动化生产监控。' -Left 36 -Top 504 -Width 860 -Height 25 -FontSize 11 -FontColor '#334A55' -Bold $true -Alignment 1 -VerticalAnchor 3
    $null = Add-Text -Slide $slide2 -Text '02' -Left 918 -Top 511 -Width 22 -Height 16 -FontSize 9 -FontColor '#7B8A92' -Bold $true -Alignment 2 -VerticalAnchor 3

    $presentation.SaveAs($outputPath, 24)
    $presentation.Export($previewDirectory, 'PNG', 1600, 900)

    Write-Output "PPT=$outputPath"
    Write-Output "PREVIEW=$previewDirectory"
}
catch {
    $line = $_.InvocationInfo.ScriptLineNumber
    $statement = $_.InvocationInfo.Line
    Write-Error ("PPT generation failed at line {0}: {1}`r`n{2}`r`n{3}" -f $line, $_.Exception.Message, $statement, $_.ScriptStackTrace)
    throw
}
finally {
    if ($null -ne $presentation) {
        $presentation.Close()
        [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($presentation)
    }
    if ($null -ne $powerPoint) {
        $powerPoint.Quit()
        [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($powerPoint)
    }
    [GC]::Collect()
    [GC]::WaitForPendingFinalizers()
}
