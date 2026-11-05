param(
    [Parameter(Mandatory = $true)][string]$ProjectCaneRoot,
    [Parameter(Mandatory = $true)][string]$OutputPath
)

$sourcePath = Join-Path $ProjectCaneRoot 'Sly1-Release\spaprops.cpp'
$source = Get-Content -LiteralPath $sourcePath -Raw
$rows = [System.Collections.Generic.List[string]]::new()
$rows.Add('// Generated from ProjectCane/Sly1-Release/spaprops.cpp. Do not hand edit.')

# SetLoDefaults only processes descriptors carrying GRFEOPID 0x800.  The
# first OPTDAT union member is four bytes wide and is deliberately populated
# with the serialized bit pattern (many float defaults are written as their
# integer IEEE-754 representation).  Preserve those bits exactly.
$defaultBits = @{}
$defaultPattern = [regex]'g_aeopid\[(\d+)\]\.optdat\.(?:fDef|gDef|nDef|oidDef|cidDef|widDef|tbidDef|sfxidDef)\s*=\s*(0x[0-9A-Fa-f]+|-?\d+)\s*;'
foreach ($match in $defaultPattern.Matches($source)) {
    $defaultBits[[int]$match.Groups[1].Value] = $match.Groups[2].Value
}
$flagsPattern = [regex]'g_aeopid\[(\d+)\]\.grfeopid\s*=\s*(0x[0-9A-Fa-f]+|-?\d+)\s*;'
foreach ($flagsMatch in $flagsPattern.Matches($source)) {
    $id = [int]$flagsMatch.Groups[1].Value
    $flagsText = $flagsMatch.Groups[2].Value
    $flags = if ($flagsText.StartsWith('0x', [StringComparison]::OrdinalIgnoreCase)) {
        [Convert]::ToInt64($flagsText.Substring(2), 16)
    } else { [Int64]::Parse($flagsText) }
    if (($flags -band 0x800) -eq 0) { continue }

    # g_aeopid is static storage. If BuildEopids does not assign the first
    # union member, SetLoDefaults reads its guaranteed all-zero bytes.
    $value = 0
    if ($defaultBits.ContainsKey($id)) {
        $valueText = $defaultBits[$id]
        $value = if ($valueText.StartsWith('0x', [StringComparison]::OrdinalIgnoreCase)) {
            [Convert]::ToUInt32($valueText.Substring(2), 16)
        } else {
            [Convert]::ToUInt32(([Int64]::Parse($valueText)) -band 0xFFFFFFFFL)
        }
    }
    $rows.Add(('BRX_OPTION_DEFAULT({0}, 0x{1:X8}u)' -f $id, $value))
}

[IO.File]::WriteAllLines($OutputPath, $rows)
