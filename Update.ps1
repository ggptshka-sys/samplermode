$ErrorActionPreference = 'Stop'
$CurrentVersion = [version]'1.0.0'
$Repository = 'ggptshka-sys/samplermode'

try {
    Write-Host 'Проверка обновлений SamplerMode...'
    if (Get-Process -Name 'reaper' -ErrorAction SilentlyContinue) {
        Write-Host 'Сохраните проект и закройте REAPER, затем снова нажмите «Обновить SamplerMode».'
        exit 2
    }

    $headers = @{ 'User-Agent' = 'SamplerMode-Updater' }
    $release = Invoke-RestMethod -Uri "https://api.github.com/repos/$Repository/releases/latest" -Headers $headers
    $latestVersion = [version]($release.tag_name.TrimStart('v'))
    if ($latestVersion -le $CurrentVersion) {
        Write-Host "Установлена последняя версия: $CurrentVersion"
        exit 0
    }

    $setup = $release.assets | Where-Object { $_.name -eq "SamplerMode-Setup-$latestVersion.exe" } | Select-Object -First 1
    $checksums = $release.assets | Where-Object { $_.name -eq 'SHA256SUMS.txt' } | Select-Object -First 1
    if (-not $setup -or -not $checksums) {
        throw 'В релизе нет установщика или файла контрольных сумм.'
    }

    $tempDirectory = Join-Path $env:TEMP "SamplerMode-Update-$latestVersion"
    New-Item -ItemType Directory -Path $tempDirectory -Force | Out-Null
    $setupPath = Join-Path $tempDirectory $setup.name
    $checksumsPath = Join-Path $tempDirectory 'SHA256SUMS.txt'
    Write-Host "Загрузка версии $latestVersion..."
    Invoke-WebRequest -Uri $setup.browser_download_url -OutFile $setupPath -Headers $headers
    Invoke-WebRequest -Uri $checksums.browser_download_url -OutFile $checksumsPath -Headers $headers

    $expectedLine = Get-Content $checksumsPath | Where-Object { $_ -match [regex]::Escape($setup.name) } | Select-Object -First 1
    $expectedHash = ($expectedLine -split '\s+')[0].ToUpperInvariant()
    $actualHash = (Get-FileHash -Algorithm SHA256 $setupPath).Hash.ToUpperInvariant()
    if (-not $expectedHash -or $actualHash -ne $expectedHash) {
        throw 'Контрольная сумма не совпала. Установка отменена.'
    }

    Write-Host 'Контрольная сумма проверена. Запуск установки...'
    $process = Start-Process -FilePath $setupPath -Verb RunAs -ArgumentList '/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART' -Wait -PassThru
    if ($process.ExitCode -ne 0) {
        throw "Установщик завершился с кодом $($process.ExitCode)."
    }
    Write-Host "SamplerMode обновлён до версии $latestVersion. Запустите REAPER 6."
} catch {
    Write-Host "Ошибка обновления: $($_.Exception.Message)"
    Write-Host 'Старая версия остаётся установленной.'
    exit 1
}
