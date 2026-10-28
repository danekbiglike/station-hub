# Изменения Station Hub для Quasar Bridge

Основано на [Quasar Bridge](https://github.com/skrashevich/quasar-bridge) Сергея Крашевича и участников, ревизия `8c1438269232171464d80b8b8fa50795cbf8c177`. Сохранены `LICENSE.upstream` и `NOTICE.upstream`; изменения к нему распространяются с сохранением условий Apache-2.0. Сторонние зависимости сохраняют собственные лицензии.

В отдельном каталоге примените изменения к точной ревизии:

```powershell
git clone https://github.com/skrashevich/quasar-bridge.git quasar-bridge
git -C quasar-bridge checkout 8c1438269232171464d80b8b8fa50795cbf8c177
git -C quasar-bridge apply ../bridge-patch/upstream.patch
Copy-Item bridge-patch/stationhub.go quasar-bridge/internal/bridge/stationhub.go
Copy-Item bridge-patch/stationhub_test.go quasar-bridge/internal/bridge/stationhub_test.go
Push-Location quasar-bridge
go test ./internal/bridge ./cmd/quasar-bridge
$env:GOOS='linux'; $env:GOARCH='arm64'; $env:CGO_ENABLED='0'
go build -o ../build/bridge ./cmd/quasar-bridge
Remove-Item Env:GOOS, Env:GOARCH, Env:CGO_ENABLED
Pop-Location
```

Пример команды предполагает, что она выполняется из корня Station Hub и каталог `quasar-bridge` создан там. Сборка использует Go-модуль upstream и скачивает его зависимости; прежде чем распространять готовые бинарные файлы, проверьте их действующие лицензии и уведомления. Для некоторых зависимостей upstream прямо отмечает отсутствие отдельной лицензии в архиве модуля.

`config.example.json` — минимальная отправная точка, не содержит данных владельца. Запуск моста не устанавливает его в прошивку и не переключает Станцию на него. Установка на другие прошивки, разрешения и последовательность возврата требуют отдельной проверки; см. `docs/ARCHITECTURE.md`.
