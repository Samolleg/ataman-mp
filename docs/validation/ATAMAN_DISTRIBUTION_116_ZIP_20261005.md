# Ataman 116 — установка и ZIP для распространения

Дата 2026-10-05. Задача EI-DISTRIBUTION-116-ZIP-20261005. Поручение владельца: «закрыл. Так же после собери версию для распространения в zip». Проверенные исходники/код main deec5d79165d4da8b4da2f7712dff63abb1cfea4. VERSION116 не увеличивается: программа повторно не менялась. Публикация не поручена и не выполнялась.

## Постоянная установка

- Повторно подтверждено отсутствие editor/runtime processes. Единственный разрешённый target: artifacts/EI-Mapper-Ataman-2-first-test.
- Из ранее проверенной чистой build-staging-116-rect-selection заменено6 managed files + SHA256SUMS.txt. Все127 managed files проверены по новому manifest.
- **18210 пользовательских файлов** вне manifest до/после совпадают по каждому SHA256; настройки, карты, сохранения, кэш и журналы не очищались.
- Verified rollback115: artifacts/rollback-EI-Mapper-Ataman-v115-to-116-20261005. После проверки обоих rollback manifests, exact paths/file set/hashes и установленного116 удалён только собственный старый rollback114.
- Доказательства: build-tests-116-rect-selection/install.log, user-files-before.json, user-files-after.json, retire-rollback.log. Установленная VERSION116; raw/staged/installed EXE hash 4A3B59E1F1793591C1CF24FD6267584F48EB6BD2027B69BF1BE2B6A48325A2B4.

## Распространяемый комплект

- Источник build-staging-116-rect-selection независимо проверен validate_distribution.ps1 до копирования. Только127 managed payloads переданы в новую собственную build-staging-116-distribution-20261005, каждый SHA256 проверен. Пользовательская установка не служит источником unlisted files.
- Добавлены START_HERE_RU.md и docs/OBJECT_ASSEMBLIES.md. Русский первый запуск включает настройку собственного профиля, оружие/броню, витрину, Рандом, сборки, новую галочку рельефа115 и исправление выделения116; только текущий CHANGELOG116.
- Raw/staged EXE и оба runtime byte-identical. Bridge SHA256 3A6504197F018A54970B3E59726476BC4BE4D3D7199085840A990DBB5A065E7C; runtime host 1361FA98B6C8805DB929123AB262861923044790A83AD5F4949C904B14DC7AB2. Qt/MinGW/compiler-runtime, лицензии и собственные распространяемые fixtures присутствуют.
- Скрытый staged --version116 exit0 выполнен до финализации. Finalizer и независимые stage/final validators PASS;129 payloads покрыты manifest. Удалён только stage logs/workflow.log от version check. Финализатор на пользовательской установке не запускался.
- Финализированная новая папка перемещена в artifacts/EI-Mapper-Ataman-116-distribution с проверкой точных исходного/конечного путей, отсутствия destination и границ workspace. После финализации программа из этой папки не запускалась. Установленные127 managed payloads и manifest при упаковке остались неизменны.
- Архив **artifacts/EI-Mapper-Ataman-116.zip**:38166156 bytes (36.40 MiB), одна верхняя папка EI-Mapper-Ataman-116,130 entries=129 payloads+SHA256SUMS.txt. ZIP exclusive-create, отсутствие linked/escaped/duplicate paths, точное покрытие files/manifest/ZIP, CRC всех entries и каждый SHA256 PASS. Manifest byte-identical финализированному источнику.
- ZIP SHA256: **47812374AFBCE80B8312192770002CC449202505AF9C270A878142BF8ABCAD05**. Sidecar artifacts/EI-Mapper-Ataman-116.zip.sha256. Скрипты/инвентари/результаты: build-tests-116-distribution-20261005/package.ps1, result.json, finalize.log, validate-source/stage/distribution.log, zip_package.py, zip-result.json.
- Игровые архивы, MOB/MPR пользователя, локальные каталоги профилей, настройки, логи, сохранения и кэш в ZIP не добавлялись. Номер116 и main сохранены, чужие dirty files не включались в коммит.

## Проверки поведения и следующая приёмка

Исправление116 ранее проверено42 focused native GL assertions +15 render queue tests PASS; creation/terrain height/assembly checks PASS. Детали: [исправление выделения](ATAMAN_RECT_SELECTION_FIX_116_20261005.md). Общий UI остаётся с10 baseline clipboard FAIL; scenario48 PASS/2 baseline FAIL; один промежуточный gallery cache FAIL, isolated+повторный PASS. Архив не исправляет эти независимые ограничения.

Ручная приёмка исходной карты не выполнена за владельца. После запуска116 протянуть рамку рядом с кроной/листвой во всех направлениях, затем пересечь видимые пиксели; повторить с Shift и кликом. Попадание на часть assembly по-прежнему выбирает сборку целиком. Получатель распаковывает всю папку, запускает EXE и настраивает свои игровые ресурсы по START_HERE_RU.md. Броня/оружие и parent112–115 сохраняют отдельную игровую приёмку.
