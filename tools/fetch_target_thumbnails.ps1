param(
    [string]$RepositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot ".."))
)

$ErrorActionPreference = "Stop"
$targetRoot = Join-Path $RepositoryRoot "app/assets/targets"

$downloads = @(
    @{ id = "tms9918a"; name = "ti99-4a.jpg"; url = "https://commons.wikimedia.org/wiki/Special:FilePath/TI-99%204A.JPG?width=640" },
    @{ id = "tms9918a"; name = "philips-vg8235.jpg"; url = "https://commons.wikimedia.org/wiki/Special:FilePath/Philips%20VG-8235%20MSX2%20computer%20(RetroMadrid%202018).jpg?width=640" },
    @{ id = "f18a"; name = "f18a-installed.jpg"; url = "https://media.invisioncic.com/r322239/monthly_2019_12/F18A-Installed.JPG.b2133b1b8119b32d7a4fed7892df597b.JPG" },
    @{ id = "f18a"; name = "f18a-mk2.jpg"; url = "https://media.invisioncic.com/r322239/monthly_06_2018/post-24952-0-65174100-1530092962.jpg" },
    @{ id = "v9938"; name = "philips-vg8235.jpg"; url = "https://commons.wikimedia.org/wiki/Special:FilePath/Philips%20VG-8235%20MSX2%20computer%20(RetroMadrid%202018).jpg?width=640" },
    @{ id = "v9938"; name = "geneve-9640.gif"; url = "https://www.ti99.com/geneve/data/images/cm9640.gif" },
    @{ id = "v9958"; name = "panasonic-fs-a1wx.jpg"; url = "https://usbsecretbase.michikusa.jp/a1fx_wx/img/fs-a1wx.jpg" },
    @{ id = "sega-sms-vdp"; name = "master-system-set.png"; url = "https://commons.wikimedia.org/wiki/Special:FilePath/Sega-Master-System-Set.png?width=640" },
    @{ id = "sega-sms-vdp"; name = "master-system-mkii.jpg"; url = "https://commons.wikimedia.org/wiki/Special:FilePath/Sega-Master-System-MkII-wController.jpg?width=640" },
    @{ id = "sega-genesis-vdp"; name = "mega-drive-genesis.jpg"; url = "https://commons.wikimedia.org/wiki/Special:FilePath/Sega%20Mega%20Drive%20and%20Genesis.jpg?width=640" },
    @{ id = "huc6270"; name = "pc-engine.jpg"; url = "https://commons.wikimedia.org/wiki/Special:FilePath/PC%20Engine%20(top%20view).jpg?width=640" },
    @{ id = "vic-ii"; name = "commodore64.jpg"; url = "https://commons.wikimedia.org/wiki/Special:FilePath/Commodore64.jpg?width=640" },
    @{ id = "vic"; name = "vic20.jpg"; url = "https://commons.wikimedia.org/wiki/Special:FilePath/Commodore%20VC20%20(41133732354).jpg?width=640" },
    @{ id = "game-boy-ppu"; name = "game-boy-original.jpg"; url = "https://commons.wikimedia.org/wiki/Special:FilePath/Game-Boy-Original.jpg?width=640" },
    @{ id = "game-boy-ppu"; name = "gameboy.jpg"; url = "https://commons.wikimedia.org/wiki/Special:FilePath/Gameboy.jpg?width=640" },
    @{ id = "game-boy-color-ppu"; name = "game-boy-color.jpg"; url = "https://commons.wikimedia.org/wiki/Special:FilePath/Wikipedia%20gameboycolor.jpg?width=640" },
    @{ id = "game-boy-color-ppu"; name = "game-boy-color-2.jpg"; url = "https://commons.wikimedia.org/wiki/Special:FilePath/GAME%20BOY%20COLOR%20(1).JPG?width=640" },
    @{ id = "super-nes-ppu"; name = "super-famicom-set.jpg"; url = "https://commons.wikimedia.org/wiki/Special:FilePath/Nintendo-Super-Famicom-Set-FL.jpg?width=640" },
    @{ id = "super-nes-ppu"; name = "super-famicom-console.jpg"; url = "https://commons.wikimedia.org/wiki/Special:FilePath/Nintendo-Super-Famicom-Console-BR.jpg?width=640" }
)

$failed = @()
foreach ($item in $downloads) {
    $directory = Join-Path $targetRoot $item.id
    New-Item -ItemType Directory -Force -Path $directory | Out-Null
    $destination = Join-Path $directory $item.name
    if ((Test-Path $destination) -and (Get-Item $destination).Length -gt 0) {
        Write-Host "Already present $($item.id)/$($item.name)"
        continue
    }
    try {
        Invoke-WebRequest -Uri $item.url -OutFile $destination -UseBasicParsing
        Write-Host "Downloaded $($item.id)/$($item.name)"
    } catch {
        $failed += "$($item.id)/$($item.name)"
        Write-Warning "Failed $($item.id)/$($item.name): $($_.Exception.Message)"
    }
}

if ($failed.Count -gt 0) {
    throw "Thumbnail download incomplete: $($failed -join ', ')"
}

Write-Host "Thumbnail download complete. Review each source-page/license entry in data/target-support/targets.json before redistribution."
