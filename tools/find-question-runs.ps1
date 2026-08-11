# Reports every run of question marks in a file, with its line.
#
# A `Set-Content -Encoding Ascii` pass turns each byte of a multi-byte UTF-8
# character into a question mark, so an em-dash becomes three and a degree sign
# two. Enumerating the runs is what says whether a repair is a single
# substitution or several.
param([Parameter(Mandatory = $true)][string]$Path)

$lines = [System.IO.File]::ReadAllLines($Path)
$counts = @{}
for ($i = 0; $i -lt $lines.Length; $i++) {
    foreach ($m in ([regex]'\?+').Matches($lines[$i])) {
        $key = $m.Value
        if (-not $counts.ContainsKey($key)) { $counts[$key] = 0 }
        $counts[$key] = $counts[$key] + 1
    }
}
foreach ($key in ($counts.Keys | Sort-Object Length)) {
    Write-Host ("run '{0}' x{1}" -f $key, $counts[$key])
}
