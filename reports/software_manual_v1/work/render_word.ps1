$ErrorActionPreference='Stop'
$base=Split-Path $PSScriptRoot
$file=Get-ChildItem -LiteralPath $base -Filter '*.docx' | Select-Object -First 1
$word=$null; $doc=$null
try {
 $word=New-Object -ComObject Word.Application
 $word.Visible=$false
 $word.DisplayAlerts=0
 $doc=$word.Documents.Open($file.FullName,$false,$false)
 $doc.Fields.Update() | Out-Null
 foreach($toc in $doc.TablesOfContents) { $toc.Update() }
 $doc.Repaginate()
 foreach($toc in $doc.TablesOfContents) { $toc.UpdatePageNumbers() }
 $doc.Save()
 $pdf=[IO.Path]::ChangeExtension($file.FullName,'.pdf')
 $doc.ExportAsFixedFormat($pdf,17)
 $count=$doc.ComputeStatistics(2)
 $meta=@{pages=$count;inlineImages=$doc.InlineShapes.Count;tables=$doc.Tables.Count;toc=$doc.TablesOfContents.Count;pdf=$pdf;wordVersion=$word.Version}
 $meta | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $PSScriptRoot 'document_validation.json') -Encoding UTF8
 $meta | ConvertTo-Json | Write-Output
 $pageSummary=@()
 for($n=1;$n -le $count;$n++) {
  $start=$doc.GoTo(1,1,$n).Start
  if($n -lt $count) {$end=$doc.GoTo(1,1,($n+1)).Start} else {$end=$doc.Content.End}
  $content=$doc.Range($start,$end).Text
  $pageSummary+=@{page=$n;characters=$content.Length;text=$content}
 }
 $pageSummary | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $PSScriptRoot 'page_text.json') -Encoding UTF8
 try {
  Add-Type -AssemblyName System.Drawing
  $doc.ActiveWindow.View.Type=3
  $preview=Join-Path $base 'preview'
  New-Item -ItemType Directory -Force $preview | Out-Null
  foreach($n in @(1,2,3,4,8,15,20,30,$count)) {
   if($n -gt $count) {continue}
   $page=$doc.ActiveWindow.Panes.Item(1).Pages.Item($n)
   $bytes=$page.EnhMetaFileBits
   $ms=[IO.MemoryStream]::new([byte[]]$bytes)
   $im=[Drawing.Image]::FromStream($ms)
   $bitmap=[Drawing.Bitmap]::new($im.Width,$im.Height)
   $graphics=[Drawing.Graphics]::FromImage($bitmap)
   $graphics.Clear([Drawing.Color]::White)
   $graphics.DrawImage($im,0,0,$im.Width,$im.Height)
   $bitmap.Save((Join-Path $preview ('page_{0:D2}.png' -f $n)),[Drawing.Imaging.ImageFormat]::Png)
   $graphics.Dispose();$bitmap.Dispose()
   $im.Dispose();$ms.Dispose()
  }
 } catch { Write-Output ('Preview: '+$_.Exception.Message) }
} finally {
 if($doc) {$doc.Close(0)}
 if($word) {$word.Quit()}
}
