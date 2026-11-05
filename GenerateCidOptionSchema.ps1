param(
    [Parameter(Mandatory = $true)][string]$ProjectCaneRoot,
    [Parameter(Mandatory = $true)][string]$OutputPath
)

$sourcePath = Join-Path $ProjectCaneRoot 'Sly1-Release\spliceobj.cpp'
$source = Get-Content -LiteralPath $sourcePath -Raw
$parseInteger = {
    param([string]$text)
    if ($text.StartsWith('0x', [StringComparison]::OrdinalIgnoreCase)) {
        return [Convert]::ToInt32($text.Substring(2), 16)
    }
    return [int]$text
}
$tables = @{}
$tablePattern = [regex]'(?s)SPLOPT\s+(g_[A-Za-z0-9_]+)\s*\[[^\]]+\]\s*=\s*\{(.*?)\n\};'
$entryPattern = [regex]'\{\s*(?:0x[0-9A-Fa-f]+|\d+)\s*,\s*&g_aeopid\[(\d+)\]'
foreach ($match in $tablePattern.Matches($source)) {
    $ids = [System.Collections.Generic.List[int]]::new()
    foreach ($entry in $entryPattern.Matches($match.Groups[2].Value)) {
        $ids.Add((& $parseInteger $entry.Groups[1].Value))
    }
    $tables[$match.Groups[1].Value] = $ids
}

$mapMatch = [regex]::Match($source, '(?s)ARYSPLOPT\s+g_mpcidarysplopt\[148\]\s*=\s*\{(.*?)\n\};')
if (-not $mapMatch.Success) { throw 'g_mpcidarysplopt was not found' }
$mapPattern = [regex]'\{\s*(0x[0-9A-Fa-f]+|\d+)\s*,\s*(g_[A-Za-z0-9_]+)\s*\}\s*,?\s*//\s*\[(\d+)\]'
$rows = [System.Collections.Generic.List[string]]::new()
$rows.Add('// Generated from ProjectCane/Sly1-Release/spliceobj.cpp. Do not hand edit.')
foreach ($entry in $mapPattern.Matches($mapMatch.Groups[1].Value)) {
    $count = & $parseInteger $entry.Groups[1].Value
    $tableName = $entry.Groups[2].Value
    $cid = [int]$entry.Groups[3].Value
    if (-not $tables.ContainsKey($tableName)) { throw "Missing table $tableName for CID $cid" }
    if ($tables[$tableName].Count -ne $count) {
        throw "Count mismatch for CID $cid ($tableName): expected $count, found $($tables[$tableName].Count)"
    }
    foreach ($optionId in $tables[$tableName]) { $rows.Add("BRX_CID_OPTION($cid, $optionId)") }
}
if (($mapPattern.Matches($mapMatch.Groups[1].Value)).Count -ne 148) { throw 'CID option map is incomplete' }
[IO.File]::WriteAllLines($OutputPath, $rows)
