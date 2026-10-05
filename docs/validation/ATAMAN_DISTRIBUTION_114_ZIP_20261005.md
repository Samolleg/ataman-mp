# Ataman114: ZIP для распространения

Задача EI-DISTRIBUTION-114-ZIP-20261005, поручение владельца2026-10-05.
main/base74595db340309f80f273c39a3e97202833dcead5, проверенный код114
21dc113ccc80b77f6c841ae063a00fb65cd96da1. Повторная упаковка не меняет VERSION;
исходники/игровые ресурсы и пользовательские данные не изменялись. Публикация
не выполнялась. Функциональные результаты114:
[броня и оружие](ATAMAN_MAP_ARMOR_114_20261005.md).

## Комплект и проверки

- Источник: проверенный чистый build-staging-114-map-armor,127 payloads.
  Независимый validator/все хеши PASS перед копированием.
- Создана новая собственная build-staging-114-distribution-20261005,
  добавлены START_HERE_RU.md и docs/OBJECT_ASSEMBLIES.md. Только текущий
  CHANGELOG114, Qt/MinGW/runtime, лицензии и исходные распространяемые fixtures;
  игровые модели/карты/текстуры/каталоги, настройки, логи и кэш не включены.
- Raw/staged editor и оба runtime byte-identical; скрытый staged --version114
  exit0 до finalizer. EXE SHA256
  50C04A603A7BE12B6B526F9EF4EE29437BBCA45104CCAA21C9A94972BD448A7A.
- Finalizer/независимые stage/final validators PASS;129 payloads покрыты manifest.
  Финализатор применялся только к новой staging-папке. После финализации GUI
  не запускался. Чистый комплект: artifacts/EI-Mapper-Ataman-114-distribution.
- Постоянная установка уже114: все127 managed files и manifest совпадают
  до/после упаковки. Повторная замена не требовалась; пользовательские файлы
  не копировались в ZIP и не менялись.
- ZIP: artifacts/EI-Mapper-Ataman-114.zip,38147394 bytes (36.38 MiB), одна
  верхняя папка EI-Mapper-Ataman-114.130 entries=129 payloads+SHA256SUMS.txt.
  Дубликатов/лишних путей нет; полное покрытие/CRC/каждый payload SHA256 и
  точное совпадение manifest с finalized source PASS без запуска GUI.
- ZIP SHA256:5770F970F3F09814445E76C959A7A99C3559ECC2614C28B75847BC95EFAEFD67.
  Отдельная контрольная сумма: artifacts/EI-Mapper-Ataman-114.zip.sha256.
  Пробники/логи/JSON: build-tests-114-distribution-20261005.

## Приёмка

Получатель распаковывает ZIP целиком, запускает EIMapperEndurance.exe и выбирает
свой профиль установленной игры/мода. Для оружия/брони нужны принадлежащие
этому профилю mapweapons.res и mapweapons-catalog.json рядом с database.res;
названия берутся из texts.res. Эти игровые ресурсы в комплект не переносились.

Проверка упаковки не устраняет прежние10 UI clipboard FAIL и не подтверждает
ручную игровую приёмку брони/оружия114 или родительских112/113. Точные проверки
тестовой карты/материалов/сохранения/сравнения с игрой указаны в START_HERE_RU.md.
