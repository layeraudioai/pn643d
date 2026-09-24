$dir1 = "DaedalusX64-3DS - Copy"
$dir2 = "DaedalusX64-3DS"

Get-ChildItem -Path $dir1 -Recurse -File | ForEach-Object {
    $relPath = $_.FullName.Substring((Resolve-Path $dir1).Path.Length + 1)
    $path2 = Join-Path $dir2 $relPath
    if (Test-Path $path2) {
        $hash1 = (Get-FileHash $_.FullName).Hash
        $hash2 = (Get-FileHash $path2).Hash
        if ($hash1 -ne $hash2) {
            Write-Output "DIFF: $relPath"
        }
    } else {
        Write-Output "MISSING: $relPath"
    }
}
