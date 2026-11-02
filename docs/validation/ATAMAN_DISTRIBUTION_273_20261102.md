# Ataman 273 — ZIP с текстурным паком, 2026-11-02

EI-DIST-273: DONE. По прямому поручению владельца подготовлен
`artifacts/EI-Mapper-Ataman-273-with-textures.zip`:
520719622 байта, 194 записи, SHA256
`9E0C016E17E2FDC65994714A3E377CAD8DECDFCA98F8C15172C960DDE9B13C7D`.

Повторён способ дистрибутива 254 из
`ATAMAN_DISTRIBUTION_254_20261031.md` и
`work/distribution-254-20261031/prepare.ps1`, receipt.json, finalizer.log.
Источник программы — принятый main
`6395e7f41cd7972c28090e57e583d753f684e5f5`, готовый чистый
`build-staging-273-mcp-units-20261102`; manifest SHA256
`9B6D3B195A5AA9924EDD357A738E9AB0417DF33D148F68CC61B6364B66538029`
сверен с package273.json и установленной 273. Пересборки нет.
Успешные проверки версии/программы для тех же байтов переиспользованы;
из distribution staging программа не запускалась.

Новый отдельный `build-staging-273-distribution-textures` создан только копированием
программных файлов из исходного manifest с проверкой SHA каждого источника.
Добавлены `TEXTURE_PACK_README.txt` по образцу 254 с номером 273 и тот же эталон
`terrain-reference/HDPack_Terrain-JamaisVu135-50c285225fb1745e.res`.
Источник текстур, как у 254: terrainAuthoringArchive из настроек постоянной установки,
`artifacts/EI-Mapper-Ataman-2-first-test/terrain-reference/HDPack_Terrain-JamaisVu135-50c285225fb1745e.res`.
Размер 682308638 байт, проверенный SHA256
`50C285225FB1745EE70D280E57A214F346481E6D380A78D55475F730D11A81B2`.
BUILD_INFO дополнен строкой о включении пака, как у 254.

Штатный `packaging/finalize_package.ps1` применён только к новому staging:
PASS, 191 прежний / 193 текущих managed-пути, 0 удалений путей и 0 удалений
stale/private/runtime-файлов. Baseline — исходный проверенный staging 273.
В архиве единственный верхний каталог `EI-Mapper-Ataman-273`.
Потоковое перечтение окончательного содержимого ZIP подтвердило точный набор
194 записей без дублей и SHA каждого payload относительно manifest, включая
сам SHA256SUMS.txt. После проверки временный ZIP переименован в окончательный.

Квитанции: `work/distribution-273-20261102/prepare.ps1`, finalizer.log,
receipt.json. Прежние ZIP, откаты, установка, настройки, карты и тестовые
копии не изменялись при упаковке. Чужие ROADMAP/render/neighbor/trace/MOB-test/QM
и .mcp.json сохранены; в коммит включены только этот отчёт и свои строки ROADMAP.
Без интерактивного запуска редактора, игры, computer-use и push.
