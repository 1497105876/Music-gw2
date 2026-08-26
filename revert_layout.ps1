$rcPath = "Z:\github\MusicPlayer2-gw\MusicPlayer2\MusicPlayer2.rc"
$content = [System.IO.File]::ReadAllText($rcPath, [System.Text.Encoding]::Unicode)

foreach ($dlg in @("IDD_STAT_ARTIST_RANK_DLG","IDD_STAT_SONG_RANK_DLG")) {
    $old = "$dlg AFX_DIALOG_LAYOUT`r`nBEGIN`r`n    1,`r`n"
    $new = "$dlg AFX_DIALOG_LAYOUT`r`nBEGIN`r`n    0,`r`n"
    if ($content.Contains($old)) {
        $content = $content.Replace([string]$old, [string]$new)
        Write-Host "$dlg layout reverted (1 -> 0)"
    }
}

[System.IO.File]::WriteAllText($rcPath, $content, [System.Text.Encoding]::Unicode)
