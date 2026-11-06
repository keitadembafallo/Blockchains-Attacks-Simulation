$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

$root = (Get-Location).Path
$reportDir = Join-Path $root 'reports'
New-Item -ItemType Directory -Force -Path $reportDir | Out-Null
$docxPath = Join-Path $reportDir 'rapport_technique_atelier2.docx'
$pdfPath = Join-Path $reportDir 'rapport_technique_atelier2.pdf'

function New-Chart([string]$name, [string]$title, [string[]]$labels, [double[]]$a, [double[]]$b, [string]$aLabel, [string]$bLabel) {
    $bitmap = New-Object System.Drawing.Bitmap 1200, 520
    $g = [System.Drawing.Graphics]::FromImage($bitmap); $g.Clear([System.Drawing.Color]::White)
    $titleFont = New-Object System.Drawing.Font('Arial', 18, [System.Drawing.FontStyle]::Bold)
    $font = New-Object System.Drawing.Font('Arial', 12)
    $blue = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(45,90,150)); $green = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(84,150,110))
    $max = [Math]::Max(1.0, (($a + $b | Measure-Object -Maximum).Maximum)); $base=430; $h=315
    $g.DrawString($title,$titleFont,[System.Drawing.Brushes]::Black,50,18); $g.DrawLine([System.Drawing.Pens]::Black,85,75,85,$base); $g.DrawLine([System.Drawing.Pens]::Black,85,$base,1130,$base)
    for($i=0;$i -lt $labels.Count;$i++) { $x=165+$i*245; $h1=[int](($a[$i]/$max)*$h); $h2=[int](($b[$i]/$max)*$h); $g.FillRectangle($blue,$x,$base-$h1,52,$h1); $g.FillRectangle($green,$x+66,$base-$h2,52,$h2); $g.DrawString($labels[$i],$font,[System.Drawing.Brushes]::Black,$x-5,445); $g.DrawString(('{0:N1}' -f $a[$i]),$font,[System.Drawing.Brushes]::Black,$x,$base-$h1-23); $g.DrawString(('{0:N1}' -f $b[$i]),$font,[System.Drawing.Brushes]::Black,$x+66,$base-$h2-23) }
    $g.FillRectangle($blue,855,44,18,18); $g.DrawString($aLabel,$font,[System.Drawing.Brushes]::Black,878,42); $g.FillRectangle($green,995,44,18,18); $g.DrawString($bLabel,$font,[System.Drawing.Brushes]::Black,1018,42)
    $bitmap.Save((Join-Path $reportDir $name),[System.Drawing.Imaging.ImageFormat]::Png); $g.Dispose();$bitmap.Dispose();$titleFont.Dispose();$font.Dispose();$blue.Dispose();$green.Dispose()
}
New-Chart 'economic_loss.png' 'Pertes economiques simulees' @('51%','Selfish','Withhold','Sybil') @(10,3.333333,63.333333,10) @(0,3.333333,-9.222222,0) 'Attaque' 'Protege'
New-Chart 'detection_recall.png' 'Rappel des methodes de detection' @('51%','Selfish','Withhold','Sybil') @(100,0,100,100) @(100,0,100,3.7037) 'Fixe %' 'Adaptatif %'

function Escape-Xml([string]$text) { return [System.Security.SecurityElement]::Escape($text) }
function Doc-P([string]$text,[string]$style='Normal',[bool]$center=$false,[bool]$bold=$false) {
    $align = if($center){'<w:jc w:val="center"/>'}else{''}; $weight=if($bold){'<w:b/>'}else{''}; $escaped=Escape-Xml $text
    return "<w:p><w:pPr><w:pStyle w:val=`"$style`"/>$align</w:pPr><w:r><w:rPr>$weight</w:rPr><w:t xml:space=`"preserve`">$escaped</w:t></w:r></w:p>"
}
function Doc-Break { return '<w:p><w:r><w:br w:type="page"/></w:r></w:p>' }
function Doc-Image([string]$relation,[int]$id,[string]$name) {
    return @"
<w:p><w:pPr><w:jc w:val="center"/></w:pPr><w:r><w:drawing><wp:inline xmlns:wp="http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing" distT="0" distB="0" distL="0" distR="0"><wp:extent cx="5486400" cy="2377440"/><wp:docPr id="$id" name="$name"/><a:graphic xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main"><a:graphicData uri="http://schemas.openxmlformats.org/drawingml/2006/picture"><pic:pic xmlns:pic="http://schemas.openxmlformats.org/drawingml/2006/picture"><pic:nvPicPr><pic:cNvPr id="0" name="$name"/><pic:cNvPicPr/></pic:nvPicPr><pic:blipFill><a:blip r:embed="$relation"/><a:stretch><a:fillRect/></a:stretch></pic:blipFill><pic:spPr><a:xfrm><a:off x="0" y="0"/><a:ext cx="5486400" cy="2377440"/></a:xfrm><a:prstGeom prst="rect"><a:avLst/></a:prstGeom></pic:spPr></pic:pic></a:graphicData></a:graphic></wp:inline></w:drawing></w:r></w:p>
"@
}

$pages = @(
    @(
        @('Rapport technique Atelier 2','Title',$true,$true),
        @('Plateforme de simulation, detection et mitigation des attaques contre une blockchain PoW PoS','Subtitle',$true,$false),
        @('Synthese experimentale fondee sur une campagne reproductible de 108 scenarios','Normal',$true,$false),
        @('','Normal',$false,$false),
        @('Etablissement  [Etablissement]','Normal',$false,$false), @('Etudiant  [Nom et prenom]','Normal',$false,$false), @('Encadrant  [Nom de l encadrant]','Normal',$false,$false), @('Annee universitaire  [2026 2027]','Normal',$false,$false),
        @('Donnees analysees  campagne complete seed 2026','Normal',$true,$false)
    ),
    @(
        @('Resume et objectif','Heading1',$false,$true),
        @('La plateforme C11 simule une blockchain hybride PoW PoS, quatre attaques, une detection explicable et des contre mesures. La campagne complete execute 108 scenarios attaques et compare pour chacun les etats NORMAL, ATTAQUE et PROTEGE, soit 324 observations dans runs.csv.','Normal',$false,$false),
        @('Resultat principal','Heading2',$false,$true),
        @('Les regles fixes detectent les scenarios 51 pour cent, block withholding et Sybil de cette campagne sans faux positif observe. Le modele actuel ne detecte pas le selfish mining genere. La protection elimine les pertes simulees pour 51 pour cent et Sybil, reduit fortement le withholding et ne reduit pas le selfish mining. Ces conclusions restent propres au simulateur.','Normal',$false,$false),
        @('Perimetre','Heading2',$false,$true), @('• Consensus par bloc PoW avec nonce ou PoS pondere par stake.','Normal',$false,$false), @('• Attaques 51 pour cent, selfish mining, withholding et Sybil P2P.','Normal',$false,$false), @('• Etats NORMAL, ATTAQUE et PROTEGE avec exports CSV et SVG.','Normal',$false,$false)
    ),
    @(
        @('Architecture de la plateforme','Heading1',$false,$true),
        @('Le noyau conserve comptes, transactions, blocs, ressources et validation. Chaque bloc memorise son parent, le consensus, le producteur, le ticket de selection et le nonce PoW. Les attaques travaillent sur des copies de chaine ou un reseau simule, sans modifier la session interactive.','Normal',$false,$false),
        @('Flux  Menu ou CLI  vers Blockchain et consensus  vers Attaques  vers Metriques  vers Detection  vers Mitigation  vers Evaluation et CSV','Normal',$true,$true),
        @('Modules','Heading2',$false,$true), @('blockchain  comptes, transactions, branches et validation.','Normal',$false,$false), @('attack_51, attack_selfish, attack_withholding, attack_sybil  scenarios dedies.','Normal',$false,$false), @('metrics  journal d evenements, orphelins, delais, recompenses et reorganisations.','Normal',$false,$false), @('detection et mitigation  alertes, gravite, noeuds suspects et decisions defensives.','Normal',$false,$false), @('evaluation et analysis  campagnes, CSV, SVG, statistiques et interpretation.','Normal',$false,$false),
        @('Reproductibilite','Heading2',$false,$true), @('Les campagnes utilisent des graines derivees deterministes et restent independantes des comptes et sessions interactives.','Normal',$false,$false)
    ),
    @(
        @('Attaques metriques detection et mitigation','Heading1',$false,$true),
        @('51 pour cent  double depense, reorganisation, orphelins et perte du marchand. Protection  six confirmations et refus de reorganisation profonde.','Normal',$false,$false),
        @('Selfish mining  blocs prives, publication tardive, course gamma et recompenses. Protection  preference pour la branche publique.','Normal',$false,$false),
        @('Block withholding  shares, blocs retenus et revenu de pool. Protection  reduction de la recompense du mineur suspect.','Normal',$false,$false),
        @('Sybil P2P  saturation des voisins, annonces retardees et vue de la victime. Protection  plafond de voisins Sybil et voisins honnetes conserves.','Normal',$false,$false),
        @('Detection fixe et adaptative','Heading2',$false,$true),
        @('La methode fixe emploie des seuils explicables sur les orphelins, les delais, les reorganisations et les recompenses. La methode adaptative construit une baseline de 20 observations normales et, apres cinq references, applique moyenne plus deux ecarts types avec des seuils planchers.','Normal',$false,$false),
        @('Limite','Heading2',$false,$true), @('Les ticks, pertes, recompenses et couts de nonce sont simules. Les alertes sont reproductibles mais ne prouvent pas la securite d un reseau reel.','Normal',$false,$false)
    ),
    @(
        @('Protocole experimental','Heading1',$false,$true),
        @('La campagne complete utilise la seed de base 2026, trois graines derivees, trois tailles de reseau 3, 5 et 8 noeuds, trois delais 1, 2 et 3 ticks et les quatre attaques. Les cas 51 pour cent et Sybil alternent PoW et PoS selon la seed ; selfish et withholding restent en PoW.','Normal',$false,$false),
        @('Parametres appliques','Heading2',$false,$true), @('Profil  complete','Normal',$false,$false), @('Volume  108 scenarios attaques et 324 etats exportes','Normal',$false,$false), @('Par groupe statistique  n egal a 27','Normal',$false,$false),
        @('Analyse','Heading2',$false,$true),
        @('analysis.c lit runs.csv et verifie detection.csv, thresholds.csv, performance.csv et aggregate.csv. Il ecrit statistics.csv, comparison.csv et interpretation.md. Les moyennes, ecarts types et IC 95 pour cent normaux sont calcules lorsque n est au moins 2. Les CSV ont ete controles sur leurs en tetes, le volume attendu et la finitude des valeurs.','Normal',$false,$false),
        @('Le temps CPU reste indicatif et depend de la machine. Les mesures deterministes prioritaires sont les ticks et le travail PoW logique.','Normal',$false,$false)
    ),
    @(
        @('Resultats statistiques','Heading1',$false,$true),
        @('Pertes economiques simulees  moyenne plus ou moins demi largeur IC 95 pour cent','Heading2',$false,$true),
        @('51 pour cent  attaque 10.00 plus ou moins 0.00   protege 0.00 plus ou moins 0.00   reduction 10.00','Normal',$false,$false),
        @('Selfish mining  attaque 3.33 plus ou moins 1.81   protege 3.33 plus ou moins 1.81   reduction 0.00','Normal',$false,$false),
        @('Withholding  attaque 63.33 plus ou moins 1.81   protege -9.22 plus ou moins 6.17   reduction 72.56','Normal',$false,$false),
        @('Sybil  attaque 10.00 plus ou moins 0.00   protege 0.00 plus ou moins 0.00   reduction 10.00','Normal',$false,$false),
        @('Les cas 51 pour cent ont 2.33 orphelins moyens sous attaque contre zero sous protection. Sybil passe de 2 a 3 ticks sous attaque puis revient a 2 ticks protege. La valeur negative de withholding protege est un credit simule de recompense evitee, pas un gain financier garanti.','Normal',$false,$false),
        @('__IMAGE1__','Image',$false,$false), @('Figure 1  Pertes economiques simulees par etat.','Caption',$true,$false)
    ),
    @(
        @('Detection et efficacite des protections','Heading1',$false,$true),
        @('51 pour cent  fixe et adaptatif  precision 1.00, rappel 1.00, FPR 0.00, tick 20.','Normal',$false,$false), @('Selfish mining  fixe et adaptatif  precision 0.00, rappel 0.00, FPR 0.00.','Normal',$false,$false), @('Withholding  fixe et adaptatif  precision 1.00, rappel 1.00, FPR 0.00, tick 20.','Normal',$false,$false), @('Sybil fixe  precision 1.00, rappel 1.00, FPR 0.00, tick 20.','Normal',$false,$false), @('Sybil adaptatif  precision 1.00, rappel 0.04, FPR 0.00, tick 20.','Normal',$false,$false),
        @('La precision parfaite est a lire avec prudence : les scenarios normaux restent synthetiques. Le rappel nul du selfish mining et le faible rappel Sybil adaptatif indiquent que les seuils et profils adverses doivent etre enrichis.','Normal',$false,$false),
        @('__IMAGE2__','Image',$false,$false), @('Figure 2  Rappel fixe et adaptatif.','Caption',$true,$false)
    ),
    @(
        @('Limites perspectives et reproduction','Heading1',$false,$true),
        @('Limites','Heading2',$false,$true), @('• Reseau synchrone simule, sans latence physique ni topologie complete.','Normal',$false,$false), @('• Transactions sans signatures ni mempool distribue complet.','Normal',$false,$false), @('• Petits echantillons par groupe et series a variance nulle.','Normal',$false,$false), @('• Temps CPU dependant de la machine.','Normal',$false,$false),
        @('Perspectives','Heading2',$false,$true), @('Les travaux suivants devraient diversifier les profils adverses, normaliser la baseline par contexte reseau, augmenter les seeds et confronter le simulateur a des traces ou un reseau plus realiste.','Normal',$false,$false),
        @('Reproduction','Heading2',$false,$true), @('1. Compiler puis executer .\\tests\\run_tests.cmd.  2. Lancer .\\atelier2.exe --campaign --campaign-profile complete --seed 2026 --evaluation-dir data\\evaluations.  3. Lancer .\\atelier2.exe --analyze data\\evaluations\\campaign_70073c59. Les CSV, SVG, statistiques et la configuration sont conserves dans le dossier de campagne.','Normal',$false,$false)
    )
)

$body = ''
foreach($page in $pages) { foreach($item in $page) { if($item[1] -eq 'Image') { $body += if($item[0] -eq '__IMAGE1__'){Doc-Image 'rId1' 1 'economic_loss'}else{Doc-Image 'rId2' 2 'detection_recall'} } else { $body += Doc-P $item[0] $item[1] $item[2] $item[3] } }; if($page -ne $pages[-1]) {$body += Doc-Break} }
$documentXml = @"
<?xml version="1.0" encoding="UTF-8" standalone="yes"?><w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><w:body>$body<w:sectPr><w:pgSz w:w="12240" w:h="15840"/><w:pgMar w:top="1020" w:right="1080" w:bottom="1020" w:left="1080"/></w:sectPr></w:body></w:document>
"@
$stylesXml = '<?xml version="1.0" encoding="UTF-8" standalone="yes"?><w:styles xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main"><w:docDefaults><w:rPrDefault><w:rPr><w:rFonts w:ascii="Arial" w:hAnsi="Arial"/><w:sz w:val="21"/></w:rPr></w:rPrDefault></w:docDefaults><w:style w:type="paragraph" w:default="1" w:styleId="Normal"><w:name w:val="Normal"/><w:pPr><w:spacing w:after="110" w:line="260" w:lineRule="auto"/></w:pPr></w:style><w:style w:type="paragraph" w:styleId="Title"><w:name w:val="Title"/><w:rPr><w:b/><w:sz w:val="40"/><w:color w:val="000000"/></w:rPr><w:pPr><w:spacing w:after="240"/></w:pPr></w:style><w:style w:type="paragraph" w:styleId="Subtitle"><w:name w:val="Subtitle"/><w:rPr><w:sz w:val="26"/></w:rPr><w:pPr><w:spacing w:after="220"/></w:pPr></w:style><w:style w:type="paragraph" w:styleId="Heading1"><w:name w:val="Heading 1"/><w:rPr><w:b/><w:sz w:val="30"/><w:color w:val="000000"/></w:rPr><w:pPr><w:spacing w:before="120" w:after="130"/></w:pPr></w:style><w:style w:type="paragraph" w:styleId="Heading2"><w:name w:val="Heading 2"/><w:rPr><w:b/><w:sz w:val="24"/><w:color w:val="000000"/></w:rPr><w:pPr><w:spacing w:before="100" w:after="80"/></w:pPr></w:style><w:style w:type="paragraph" w:styleId="Caption"><w:name w:val="Caption"/><w:rPr><w:i/><w:sz w:val="17"/></w:rPr><w:pPr><w:spacing w:after="80"/></w:pPr></w:style></w:styles>'

if(Test-Path -LiteralPath $docxPath){Remove-Item -LiteralPath $docxPath -Force}
$zip=[System.IO.Compression.ZipFile]::Open($docxPath,[System.IO.Compression.ZipArchiveMode]::Create)
function Add-ZipText($zip,$name,$value){$e=$zip.CreateEntry($name);$w=New-Object System.IO.StreamWriter($e.Open(),(New-Object System.Text.UTF8Encoding($false)));$w.Write($value);$w.Dispose()}
function Add-ZipFile($zip,$name,$path){$e=$zip.CreateEntry($name);$s=$e.Open();$f=[System.IO.File]::OpenRead($path);$f.CopyTo($s);$f.Dispose();$s.Dispose()}
Add-ZipText $zip '[Content_Types].xml' '<?xml version="1.0" encoding="UTF-8" standalone="yes"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="xml" ContentType="application/xml"/><Default Extension="png" ContentType="image/png"/><Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/><Override PartName="/word/styles.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml"/></Types>'
Add-ZipText $zip '_rels/.rels' '<?xml version="1.0" encoding="UTF-8" standalone="yes"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="word/document.xml"/></Relationships>'
Add-ZipText $zip 'word/document.xml' $documentXml; Add-ZipText $zip 'word/styles.xml' $stylesXml
Add-ZipText $zip 'word/_rels/document.xml.rels' '<?xml version="1.0" encoding="UTF-8" standalone="yes"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/image" Target="media/economic_loss.png"/><Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/image" Target="media/detection_recall.png"/><Relationship Id="rId3" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/></Relationships>'
Add-ZipFile $zip 'word/media/economic_loss.png' (Join-Path $reportDir 'economic_loss.png'); Add-ZipFile $zip 'word/media/detection_recall.png' (Join-Path $reportDir 'detection_recall.png'); $zip.Dispose()

function Pdf-Escape([string]$s){return $s.Replace('\\','\\\\').Replace('(','\\(').Replace(')','\\)')}
function Pdf-Text([string]$text,[double]$x,[double]$y,[int]$size=10,[bool]$bold=$false){$font=if($bold){'F2'}else{'F1'};return "BT /$font $size Tf $x $y Td ($(Pdf-Escape $text)) Tj ET`n"}
function Pdf-Wrap([string]$text){$words=$text -split ' ';$lines=@();$line='';foreach($word in $words){if(($line+' '+$word).Length -gt 92){$lines+=$line;$line=$word}else{$line=($line+' '+$word).Trim()}};if($line){$lines+=$line};return $lines}
$contents=@(); $pdfPage=0
foreach($page in $pages){$pdfPage++;$stream="0.10 0.19 0.34 rg 0 756 612 36 re f`n"; $stream+=Pdf-Text 'Atelier 2  Rapport technique' 34 770 11 $true; $y=724
foreach($item in $page){if($item[1] -eq 'Image'){continue};$size=if($item[1] -eq 'Title'){24}elseif($item[1] -eq 'Subtitle'){15}elseif($item[1] -eq 'Heading1'){16}elseif($item[1] -eq 'Heading2'){12}else{10};$isBold=$item[3] -or $item[1] -match 'Heading|Title'; foreach($line in (Pdf-Wrap $item[0])){$stream+=Pdf-Text $line 42 $y $size $isBold;$y-= if($item[1] -match 'Heading|Title'){24}else{15}};if($item[1] -match 'Heading'){$y-=4}}
if($pdfPage -eq 6){$stream+="0.18 0.35 0.59 rg 100 125 55 32 re f 0.33 0.59 0.43 rg 165 157 55 0 re f 0.18 0.35 0.59 rg 280 35 55 122 re f 0.33 0.59 0.43 rg 345 175 55 0 re f 0.18 0.35 0.59 rg 460 125 55 32 re f`n"};if($pdfPage -eq 7){$stream+="0.18 0.35 0.59 rg 100 80 45 120 re f 0.33 0.59 0.43 rg 155 80 45 120 re f 0.18 0.35 0.59 rg 250 200 45 0 re f 0.33 0.59 0.43 rg 305 200 45 0 re f 0.18 0.35 0.59 rg 400 80 45 120 re f 0.33 0.59 0.43 rg 455 80 45 120 re f`n"};$stream+=Pdf-Text ("Page $pdfPage") 280 25 8 $false;$contents+=$stream}
$enc=[System.Text.Encoding]::GetEncoding(1252);$objects=@();$pageIds=@();$next=4;foreach($c in $contents){$contentId=$next;$pageId=$next+1;$next+=2;$bytes=$enc.GetBytes($c);$objects+="<< /Length $($bytes.Length) >>`nstream`n$c`nendstream";$objects+="<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << /Font << /F1 3 0 R /F2 4 0 R >> >> /Contents $contentId 0 R >>";$pageIds+=$pageId}
# Object ids are rebuilt so that F2 is object 4 and page objects follow it.
$objects=@('<< /Type /Catalog /Pages 2 0 R >>',("<< /Type /Pages /Count $($contents.Count) /Kids [" + (($pageIds | ForEach-Object {"$_ 0 R"}) -join ' ') + '] >>'),'<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>','<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold >>')
$pageIds=@();$next=5;foreach($c in $contents){$contentId=$next;$pageId=$next+1;$next+=2;$bytes=$enc.GetBytes($c);$objects+="<< /Length $($bytes.Length) >>`nstream`n$c`nendstream";$objects+="<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << /Font << /F1 3 0 R /F2 4 0 R >> >> /Contents $contentId 0 R >>";$pageIds+=$pageId}
$objects[1]="<< /Type /Pages /Count $($contents.Count) /Kids ["+(($pageIds|ForEach-Object{"$_ 0 R"}) -join ' ')+'] >>'
$ms=New-Object System.IO.MemoryStream;$writer=New-Object System.IO.StreamWriter($ms,$enc);$writer.NewLine="`n";$writer.Write('%PDF-1.4' + "`n%âãÏÓ`n");$writer.Flush();$offsets=@(0);for($i=0;$i -lt $objects.Count;$i++){$offsets+=$ms.Position;$writer.Write("$($i+1) 0 obj`n$($objects[$i])`nendobj`n");$writer.Flush()};$xref=$ms.Position;$writer.Write("xref`n0 $($objects.Count+1)`n0000000000 65535 f `n");for($i=1;$i -lt $offsets.Count;$i++){$writer.Write(('{0:0000000000} 00000 n ' -f $offsets[$i])+"`n")};$writer.Write("trailer`n<< /Size $($objects.Count+1) /Root 1 0 R >>`nstartxref`n$xref`n%%EOF");$writer.Flush();[System.IO.File]::WriteAllBytes($pdfPath,$ms.ToArray());$writer.Dispose();$ms.Dispose()
Write-Output "DOCX: $docxPath"; Write-Output "PDF: $pdfPath"
