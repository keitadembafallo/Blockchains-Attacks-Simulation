$ErrorActionPreference = 'Stop'

$root = (Get-Location).Path
$campaign = Join-Path $root 'data\evaluations\campaign_70073c59'
$reportDir = Join-Path $root 'reports'
$docxPath = Join-Path $reportDir 'rapport_technique_atelier2.docx'
$pdfPath = Join-Path $reportDir 'rapport_technique_atelier2.pdf'
New-Item -ItemType Directory -Force -Path $reportDir | Out-Null

$word = New-Object -ComObject Word.Application
$word.Visible = $false
$doc = $word.Documents.Add()

$wdCollapseEnd = 0
$wdAlignLeft = 0
$wdAlignCenter = 1
$wdPageBreak = 7
$wdStyleTitle = -63
$wdStyleHeading1 = -2
$wdStyleHeading2 = -3
$wdFormatDocumentDefault = 16
$wdExportFormatPDF = 17

$doc.PageSetup.PaperSize = 1
$doc.PageSetup.TopMargin = $word.CentimetersToPoints(1.8)
$doc.PageSetup.BottomMargin = $word.CentimetersToPoints(1.7)
$doc.PageSetup.LeftMargin = $word.CentimetersToPoints(1.9)
$doc.PageSetup.RightMargin = $word.CentimetersToPoints(1.9)

function Set-RunFont($range, [double]$size = 10.5, [bool]$bold = $false, [string]$name = 'Aptos') {
    $range.Font.Name = $name
    $range.Font.Size = $size
    $range.Font.Bold = [int]$bold
    $range.Font.Color = 0
}

function Add-Paragraph([string]$text, [int]$alignment = 0, [double]$size = 10.5, [bool]$bold = $false, [int]$style = 0, [double]$after = 5) {
    $range = $doc.Range($doc.Content.End - 1, $doc.Content.End - 1)
    $range.InsertAfter($text + "`r")
    $p = $doc.Paragraphs.Item($doc.Paragraphs.Count)
    if ($style -ne 0) { $p.Range.Style = $style }
    $p.Alignment = $alignment
    $p.SpaceAfter = $after
    $p.SpaceBefore = 0
    Set-RunFont $p.Range $size $bold
    return $p
}

function Add-Heading([string]$text, [int]$level = 1) {
    $style = if ($level -eq 1) { $wdStyleHeading1 } else { $wdStyleHeading2 }
    $fontSize = if ($level -eq 1) { 14 } else { 11.5 }
    $p = Add-Paragraph $text $wdAlignLeft $fontSize $true $style 7
    $p.SpaceBefore = if ($level -eq 1) { 9 } else { 5 }
}

function Add-Bullet([string]$text) {
    $p = Add-Paragraph $text $wdAlignLeft 10.2 $false 0 2
    $p.Range.ListFormat.ApplyBulletDefault() | Out-Null
}

function Add-Table([string[]]$headers, [object[][]]$rows, [double[]]$widths) {
    $range = $doc.Range($doc.Content.End - 1, $doc.Content.End - 1)
    $table = $doc.Tables.Add($range, $rows.Count + 1, $headers.Count)
    $table.Borders.Enable = 1
    $table.Range.Font.Name = 'Aptos'
    $table.Range.Font.Size = 8.5
    $table.Rows.Item(1).Range.Font.Bold = 1
    $table.Rows.Item(1).Range.Font.Color = 16777215
    $table.Rows.Item(1).Shading.BackgroundPatternColor = 6299648
    for ($c = 1; $c -le $headers.Count; $c++) {
        $table.Cell(1, $c).Range.Text = [string]$headers[$c - 1]
        if ($widths.Count -ge $c) { $table.Columns.Item($c).Width = $word.CentimetersToPoints($widths[$c - 1]) }
    }
    for ($r = 0; $r -lt $rows.Count; $r++) {
        for ($c = 0; $c -lt $headers.Count; $c++) {
            $cell = $table.Cell($r + 2, $c)
            $cell.Range.Text = [string]$rows[$r][$c]
            $cell.VerticalAlignment = 1
            if (($r % 2) -eq 1) { $cell.Shading.BackgroundPatternColor = 15921906 }
            if ($c -gt 0) { $cell.Range.ParagraphFormat.Alignment = $wdAlignCenter }
        }
    }
    foreach ($row in $table.Rows) { $row.AllowBreakAcrossPages = $false }
    $p = Add-Paragraph '' $wdAlignLeft 5 $false 0 2
    return $table
}

function Add-Figure([string]$file, [string]$caption, [double]$widthCm) {
    $path = Join-Path $reportDir ([System.IO.Path]::GetFileNameWithoutExtension($file) + '.png')
    if (Test-Path -LiteralPath $path) {
        $range = $doc.Range($doc.Content.End - 1, $doc.Content.End - 1)
        $shape = $doc.InlineShapes.AddPicture($path, $false, $true, $range)
        $shape.Width = $word.CentimetersToPoints($widthCm)
        $p = Add-Paragraph $caption $wdAlignCenter 8.5 $false 0 5
        $p.Range.Font.Italic = 1
    }
}

function New-BarChart([string]$fileName, [string]$title, [string[]]$labels, [double[]]$first, [double[]]$second, [string]$firstLabel, [string]$secondLabel) {
    Add-Type -AssemblyName System.Drawing
    $bitmap = New-Object System.Drawing.Bitmap 1200, 520
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.Clear([System.Drawing.Color]::White)
    $fontTitle = New-Object System.Drawing.Font('Aptos', 18, [System.Drawing.FontStyle]::Bold)
    $font = New-Object System.Drawing.Font('Aptos', 12)
    $brushText = [System.Drawing.Brushes]::Black
    $brushA = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(45, 90, 150))
    $brushB = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(84, 150, 110))
    $max = [Math]::Max(1.0, (($first + $second | Measure-Object -Maximum).Maximum))
    $graphics.DrawString($title, $fontTitle, $brushText, 52, 20)
    $left = 90; $bottom = 430; $height = 320; $barWidth = 52
    $graphics.DrawLine([System.Drawing.Pens]::Black, $left, 80, $left, $bottom)
    $graphics.DrawLine([System.Drawing.Pens]::Black, $left, $bottom, 1130, $bottom)
    for ($i = 0; $i -lt $labels.Count; $i++) {
        $x = 165 + $i * 245
        $h1 = [int](($first[$i] / $max) * $height)
        $h2 = [int](($second[$i] / $max) * $height)
        $graphics.FillRectangle($brushA, $x, $bottom - $h1, $barWidth, $h1)
        $graphics.FillRectangle($brushB, $x + 65, $bottom - $h2, $barWidth, $h2)
        $graphics.DrawString($labels[$i], $font, $brushText, $x - 8, 445)
        $graphics.DrawString(('{0:N1}' -f $first[$i]), $font, $brushText, $x, $bottom - $h1 - 24)
        $graphics.DrawString(('{0:N1}' -f $second[$i]), $font, $brushText, $x + 65, $bottom - $h2 - 24)
    }
    $graphics.FillRectangle($brushA, 860, 42, 18, 18); $graphics.DrawString($firstLabel, $font, $brushText, 884, 40)
    $graphics.FillRectangle($brushB, 1000, 42, 18, 18); $graphics.DrawString($secondLabel, $font, $brushText, 1024, 40)
    $bitmap.Save((Join-Path $reportDir $fileName), [System.Drawing.Imaging.ImageFormat]::Png)
    $graphics.Dispose(); $bitmap.Dispose(); $fontTitle.Dispose(); $font.Dispose(); $brushA.Dispose(); $brushB.Dispose()
}

function Add-PageBreak { $doc.Range($doc.Content.End - 1, $doc.Content.End - 1).InsertBreak($wdPageBreak) }

New-BarChart 'economic_loss.png' 'Pertes économiques simulées' @('51%', 'Selfish', 'Withhold', 'Sybil') @(10, 3.333333, 63.333333, 10) @(0, 3.333333, -9.222222, 0) 'Attaque' 'Protégé'
New-BarChart 'detection_recall.png' 'Rappel des méthodes de détection' @('51%', 'Selfish', 'Withhold', 'Sybil') @(100, 0, 100, 100) @(100, 0, 100, 3.7037) 'Fixe %' 'Adaptatif %'
New-BarChart 'attack_success.png' 'Scénarios réussis et impact réduit' @('51%', 'Selfish', 'Withhold', 'Sybil') @(27, 0, 27, 27) @(27, 0, 27, 27) 'Attaques réussies' 'Impact réduit'

# Header and footer
$header = $doc.Sections.Item(1).Headers.Item(1).Range
$header.Text = 'Atelier 2  Simulation et protection de blockchain'
Set-RunFont $header 8.5 $false
$header.ParagraphFormat.Alignment = $wdAlignCenter
$footer = $doc.Sections.Item(1).Footers.Item(1).Range
$footer.Text = 'Rapport technique  |  '
$footer.Fields.Add($footer, -1) | Out-Null
Set-RunFont $footer 8.5 $false
$footer.ParagraphFormat.Alignment = $wdAlignCenter

# Page 1 cover
$doc.Paragraphs.Item(1).Range.Text = ''
for ($i = 0; $i -lt 7; $i++) { Add-Paragraph '' $wdAlignCenter 10 $false 0 4 | Out-Null }
$title = Add-Paragraph 'Rapport technique Atelier 2' $wdAlignCenter 23 $true $wdStyleTitle 10
$title.Range.Font.Color = 0
Add-Paragraph 'Plateforme de simulation, détection et mitigation des attaques contre une blockchain PoW PoS' $wdAlignCenter 13 $false 0 20 | Out-Null
Add-Paragraph 'Synthèse expérimentale fondée sur une campagne reproductible de 108 scénarios' $wdAlignCenter 11 $false 0 35 | Out-Null
Add-Table @('Champ', 'À compléter') @(
    @('Établissement', '[Établissement]'),
    @('Étudiant', '[Nom et prénom]'),
    @('Encadrant', '[Nom de l encadrant]'),
    @('Année universitaire', '[2026 2027]')
) @(4.2, 10.2) | Out-Null
Add-Paragraph 'Données analysées  campagne complete seed 2026' $wdAlignCenter 9.5 $false 0 0 | Out-Null
Add-PageBreak

# Page 2
Add-Heading 'Résumé et objectif'
Add-Paragraph 'Ce rapport évalue une plateforme C11 de simulation d une blockchain hybride PoW PoS. Elle produit des blocs honnêtes, rejoue quatre familles d attaques, collecte des événements, applique une détection explicable et simule des contre mesures. La campagne complète a exécuté 108 scénarios d attaque, chacun comparé à un état normal et à un état protégé, soit 324 lignes d observation dans runs.csv.'
Add-Paragraph 'Le résultat principal est nuancé. Les scénarios 51 pour cent, block withholding et Sybil sont détectés par la méthode fixe dans cette campagne, avec un rappel de 100 pour cent et aucun faux positif observé. Le modèle actuel ne détecte pas le selfish mining dans les profils générés. La protection élimine les pertes simulées pour 51 pour cent et Sybil, réduit fortement la perte associée au withholding, mais ne réduit pas celle du selfish mining. Ces résultats décrivent le simulateur et ses règles, pas un réseau blockchain réel.'
Add-Heading 'Périmètre étudié' 2
Add-Bullet 'Consensus par bloc  PoW avec sélection pondérée et recherche de nonce, ou PoS avec sélection pondérée par stake.'
Add-Bullet 'Attaques  51 pour cent avec double dépense, selfish mining, block withholding et isolement Sybil P2P.'
Add-Bullet 'Comparaison systématique des états NORMAL, ATTAQUE et PROTEGE, avec export CSV et graphiques SVG.'
Add-Heading 'Contribution réalisée dans le projet' 2
Add-Paragraph 'Le projet sépare le cœur blockchain, les scénarios d attaque, la collecte de métriques, la détection, la mitigation, l évaluation et l analyse. Cette organisation permet de rejouer une expérience par ligne de commande, d inspecter les CSV et de modifier une famille d attaque sans réécrire le registre ou la validation de chaîne.'
Add-PageBreak

# Page 3
Add-Heading 'Architecture de la plateforme'
Add-Paragraph 'Le noyau conserve les comptes, transactions, blocs, ressources et validation d intégrité. Un bloc mémorise son parent, son consensus, son producteur, son ticket de sélection et, en PoW, son nonce. Les attaques travaillent sur des copies de chaîne ou sur un réseau P2P simulé afin de ne pas modifier la session interactive tant qu une adoption n est pas autorisée.'
Add-Paragraph 'Flux d exécution' $wdAlignCenter 10.5 $true 0 7 | Out-Null
Add-Paragraph 'Menu ou CLI  →  Blockchain et consensus  →  Modules d attaque  →  Métriques  →  Détection  →  Mitigation  →  Évaluation et CSV' $wdAlignCenter 10.5 $true 0 12 | Out-Null
Add-Heading 'Modules principaux' 2
Add-Table @('Module', 'Rôle') @(
    @('blockchain', 'Comptes, transactions, blocs, PoW PoS, validation et branches.'),
    @('attack 51 selfish withholding sybil', 'Scénarios isolés et traces de comportements malveillants.'),
    @('metrics', 'Journal d événements, blocs, orphelins, délais, récompenses et réorganisations.'),
    @('detection', 'Scores fixe et adaptatif, alertes, gravité et nœuds suspects.'),
    @('mitigation', 'Décisions de protection, sanctions de pool, voisinage Sybil et refus de réorganisation.'),
    @('evaluation et analysis', 'Grilles reproductibles, CSV, SVG, statistiques et interprétation.')
) @(5.0, 9.4) | Out-Null
Add-Heading 'Persistance et reproductibilité' 2
Add-Paragraph 'La session interactive conserve les comptes, la chaîne et les files d attente. Les campagnes n utilisent pas cette session. Elles dérivent leurs graines de manière déterministe à partir de la seed de base, enregistrent les paramètres appliqués et écrivent leurs résultats dans un dossier identifié par la configuration. Cette séparation évite qu une expérience dépende d un état local antérieur.'
Add-PageBreak

# Page 4
Add-Heading 'Attaques métriques détection et mitigation'
Add-Table @('Famille', 'Signal mesuré', 'Protection simulée') @(
    @('51 pour cent', 'Double dépense, réorganisation, blocs orphelins, perte du marchand.', 'Au moins six confirmations et refus de réorganisation profonde.'),
    @('Selfish mining', 'Blocs privés, publication tardive, course gamma, récompenses.', 'Préférence pour la branche publique en cas de publication tardive.'),
    @('Block withholding', 'Shares, blocs retenus, revenu de pool et mineurs suspects.', 'Réduction ou annulation de la part de récompense du mineur reteneur.'),
    @('Sybil P2P', 'Saturation des voisins, annonces retardées et vue de la victime.', 'Plafond de voisins Sybil à 50 pour cent et maintien de voisins honnêtes.')
) @(3.3, 5.5, 5.6) | Out-Null
Add-Heading 'Règles de détection' 2
Add-Paragraph 'La détection fixe applique des règles explicables. Une double dépense réussie associée à une réorganisation et à des orphelins déclenche une alerte forte. Les blocs privés et la sur rémunération de coalition visent le selfish mining. Les blocs retenus et la baisse de revenu de pool visent le withholding. La saturation des voisins et un délai supérieur à la référence visent Sybil. Une chaîne invalide obtient directement une gravité critique.'
Add-Paragraph 'La variante adaptative conserve uniquement les observations normales dans une fenêtre glissante de 20 exécutions. Après cinq références, elle compare la métrique à moyenne plus deux écarts types, tout en conservant des minima pédagogiques  cinq points de taux d orphelins, un tick de délai et dix points de concentration. Avant cette phase de chauffe, elle retombe sur les seuils fixes.'
Add-Heading 'Limite conceptuelle' 2
Add-Paragraph 'Les délais sont des ticks logiques. Les récompenses, pertes économiques et coûts de nonce sont des valeurs de simulation. Les alertes sont donc interprétables et reproductibles, mais elles ne constituent pas une preuve de sécurité ni une mesure de performance d une blockchain distribuée en production.'
Add-PageBreak

# Page 5
Add-Heading 'Protocole expérimental'
Add-Paragraph 'La campagne complete a été lancée avec la seed de base 2026. Elle utilise trois graines dérivées, trois tailles de réseau 3, 5 et 8 nœuds, trois délais de propagation 1, 2 et 3 ticks et les quatre attaques. Chaque combinaison attaque produit un scénario attaqué et deux références de comparaison  NORMAL et PROTEGE. Les attaques 51 pour cent et Sybil alternent PoW et PoS selon la seed ; selfish mining et withholding restent en PoW.'
Add-Table @('Élément', 'Valeur appliquée') @(
    @('Profil', 'complete'),
    @('Seed de base', '2026  avec 3 graines dérivées'),
    @('Réseaux', '3  5  8 nœuds'),
    @('Délais', '1  2  3 ticks'),
    @('Attaques', '51 pour cent  selfish  withholding  Sybil'),
    @('Volume', '108 scénarios attaqués  324 états exportés')
) @(5.2, 9.2) | Out-Null
Add-Heading 'Mesures et traitement statistique' 2
Add-Paragraph 'analysis.c lit runs.csv ainsi que les exports de détection, seuils, performances et agrégats. Pour chaque attaque et état, il calcule l effectif, la moyenne, l écart type et un intervalle de confiance normal à 95 pour cent lorsque n est au moins égal à 2. Chaque groupe du présent rapport comporte n égal à 27. Les fichiers statistics.csv, comparison.csv et interpretation.md ont été générés dans le dossier de campagne.'
Add-Paragraph 'Les sorties ont été contrôlées sur trois points  présence des 324 états attendus, en têtes CSV disponibles et chiffres finis. Les cinq graphiques SVG de la campagne ont été produits avant l analyse. Les temps CPU sont volontairement rapportés comme indicatifs, car ils dépendent de la machine d exécution.'
Add-PageBreak

# Page 6
Add-Heading 'Résultats statistiques'
Add-Paragraph 'Le tableau suivant résume les pertes économiques simulées. Les intervalles sont des demi largeurs d IC 95 pour cent. Une perte négative dans le cas protected withholding correspond au crédit simulé de récompense évitée dans le modèle de mitigation et ne doit pas être interprétée comme un gain monétaire réel garanti.'
Add-Table @('Attaque', 'Attaque moyenne ± IC95', 'Protégé moyenne ± IC95', 'Réduction moyenne') @(
    @('51 pour cent', '10.00 ± 0.00', '0.00 ± 0.00', '10.00'),
    @('Selfish mining', '3.33 ± 1.81', '3.33 ± 1.81', '0.00'),
    @('Withholding', '63.33 ± 1.81', '-9.22 ± 6.17', '72.56'),
    @('Sybil', '10.00 ± 0.00', '0.00 ± 0.00', '10.00')
) @(3.4, 4.5, 4.5, 2.3) | Out-Null
Add-Paragraph 'Les scénarios 51 pour cent produisent en moyenne 2.33 blocs orphelins dans l état attaqué, contre zéro dans les états normal et protégé. Les délais moyens de Sybil passent de 2 à 3 ticks sous attaque puis reviennent à 2 ticks avec la protection. Pour le withholding, le travail PoW logique moyen passe de 20 dans l état normal à 13.67 sous attaque et sous protection, ce qui reflète les blocs retenus par le modèle.'
Add-Figure 'economic_loss.svg' 'Figure 1  Perte économique moyenne par état dans la campagne complète' 13.2
Add-PageBreak

# Page 7
Add-Heading 'Résultats de détection et efficacité des protections'
Add-Table @('Attaque', 'Méthode', 'Précision', 'Rappel', 'FPR', 'Tick moyen') @(
    @('51 pour cent', 'Fixe et adaptatif', '1.00', '1.00', '0.00', '20'),
    @('Selfish mining', 'Fixe et adaptatif', '0.00', '0.00', '0.00', '0'),
    @('Withholding', 'Fixe et adaptatif', '1.00', '1.00', '0.00', '20'),
    @('Sybil', 'Fixe', '1.00', '1.00', '0.00', '20'),
    @('Sybil', 'Adaptatif', '1.00', '0.04', '0.00', '20')
) @(3.7, 3.3, 2.0, 1.8, 1.6, 1.9) | Out-Null
Add-Paragraph 'La précision à 1.00 doit être lue avec prudence. Les scénarios normaux de cette campagne ne génèrent aucun faux positif, mais ils restent synthétiques. Le rappel nul pour selfish mining est le résultat le plus important pour l amélioration du détecteur  les profils générés ne franchissent pas les règles de blocs privés, orphelins et sur rémunération. Le rappel adaptatif très faible pour Sybil montre aussi que la phase de chauffe et la variation de baseline peuvent réduire la sensibilité.'
Add-Figure 'detection_recall.svg' 'Figure 2  Rappel de détection exporté par la campagne' 12.8
Add-Figure 'attack_success.svg' 'Figure 3  Succès des attaques et blocage par les protections' 12.8
Add-PageBreak

# Page 8
Add-Heading 'Interprétation limites et perspectives'
Add-Paragraph 'Les résultats confirment l intérêt d un pipeline qui relie événements, métriques, détection et mitigation. Dans les scénarios où les signaux correspondent aux règles, la plateforme empêche ou réduit l impact simulé. La couverture est inégale. Le selfish mining demande des métriques ou seuils mieux calibrés, et le Sybil adaptatif doit être évalué après une baseline normale plus riche et plus variée.'
Add-Heading 'Limites identifiées' 2
Add-Bullet 'Réseau synchrone simulé  les ticks ne modélisent ni latence physique, ni perte de paquets, ni topologie réelle.'
Add-Bullet 'Transactions sans signatures cryptographiques ni mempool distribué complet.'
Add-Bullet 'Petits échantillons par groupe  n égal à 27  et plusieurs séries à variance nulle, ce qui limite la portée des intervalles.'
Add-Bullet 'Temps CPU dépendant de la machine ; les comparaisons déterministes reposent plutôt sur ticks et travail PoW logique.'
Add-Heading 'Perspectives' 2
Add-Paragraph 'Les prochaines améliorations devraient enrichir les profils adverses, utiliser une baseline multi contexte normalisée par délai, mesurer précision et rappel sur davantage de seeds, puis relier les alertes à des contre mesures réglables. Une validation sur un simulateur réseau plus réaliste ou des traces publiques serait nécessaire avant toute conclusion opérationnelle.'
Add-Heading 'Reproduction' 2
Add-Paragraph '1. Compiler et exécuter .\tests\run_tests.cmd.  2. Lancer .\atelier2.exe --campaign --campaign-profile complete --seed 2026 --evaluation-dir data\evaluations.  3. Exécuter .\atelier2.exe --analyze data\evaluations\campaign_70073c59. Les CSV, les cinq SVG, les statistiques et la configuration sont alors regroupés dans le dossier de campagne. Le README détaille également les options de campagne et les limites des mesures.'

$doc.SaveAs2($docxPath, $wdFormatDocumentDefault)
$doc.ExportAsFixedFormat($pdfPath, $wdExportFormatPDF)
$doc.Close()
$word.Quit()

Write-Host "DOCX: $docxPath"
Write-Host "PDF: $pdfPath"
