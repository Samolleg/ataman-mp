# Ataman122 — сохранение высот без перезаписи RES

Задача EI-TERRAIN-HEIGHT-RES-SAVE-FIX-122-20261007; поручение владельца
2026-10-07 «делай правку» после аудита121 и вопроса о контрольной сумме.
База main `5f080a69c1524f682fa9f5bdb64fd580e5b905dd`, VERSION121→122.
Обычный код проекта и собственные синтетические данные. Живые игровые
MPR/MOB/RES не изменялись; приватные движковые источники не использовались.
Предшествующая причина и native воспроизведение121 —
[аудит](ATAMAN_TERRAIN_HEIGHT_RES_SAVE_AUDIT_121_20261007.md).

## Исправление

- Обычный save больше не публикует атлас только потому, что профиль обозначен
  как ранее сохранённый рабочий черновик. Публикация нужна для реально
  используемых новых физических тайлов или явно запрошенной смены identity.
  Save As рабочего черновика сохраняет прежний путь публикации нового назначения.
- Служебное поле материалов теперь сохраняется вместе с MPR в принадлежащем
  редактору члене `ei_mapper_terrain_semantics.json`. Height-only и покраска
  существующими физическими тайлами используют одну атомарную MPR-транзакцию
  с проверкой источника, резервной копией и reopen. RES не включается в этот save.
- JSON v2 проверяет namespace, размер сетки, MP-параметры тайлов/материалов и
  анимации, SEC-ссылки на тайлы и материалы покрытий, atlas/profile SHA256.
  Вершины, нормали и maxZ исключены из material binding. Изменение только
  пробелов JSON-профиля не меняет привязку; изменение содержания отказывает.
- Legacy v1 из RES читается с прежним строгим полным MPR hash. При первом
  последующем save возникает v2 внутри MPR, который имеет приоритет при reopen;
  старая служебная запись RES сохраняется побайтно.
- Добавление служебного члена append-only: сохраняются все старые bytes тела,
  unknown payloads, имена/кодировка, offsets и timestamps; перестраивается
  native hash-chain table. Нераспознанный payload с зарезервированным именем
  отклоняется. Повтор того же поля является побайтным no-op для этого члена.
- Общий save проверяет фактические используемые слоты. Уже загруженный atlas
  без новой покраски не добавляет RES в batch. Используемая новая страница
  по-прежнему сохраняется вместе с MPR/profile/semantic data с прежним rollback.
  Неиспользуемая после Undo новая текстура не требует публикации.

## Проверки

| Проверка | Результат |
|---|---|
| Fresh release122, raw/staged `--version` | PASS, версия122 |
| `--terrain-height-res-save-check`, production local+world save | **29 PASS,0 FAIL** |
| `endurance_document_tests` | **192 PASS,0 FAIL,18 SKIP** |
| Финальный native `--ui-widget-check` | **169 PASS,0 FAIL**, exit0 |
| `--terrain-mob-height-check`, окончательный EXE | **19 PASS,0 FAIL** |
| `--scenario-layer-check`, окончательный EXE | **50 PASS,0 FAIL** |
| `--world-map-snap-check`, окончательный EXE | **32 PASS,0 FAIL** |
| Fresh staging finalizer + independent validator | **PASS**,127 managed payloads |

Новый native check использует distributable `particle-flat-7x5.mpr` для
собственного flat canvas в QTemporaryDir, собственный DXT1 RES и hidden CView.
Он вызывает настоящие `saveChangedLandscapeDocument` и `saveWorldTerrainMaps`.
Height-only оставляет **bytes, lastModified и число RES-preflight calls прежними**.
Проверены два последовательных сохранения, копия с другим именем, fresh reopen
точных материалов, legacy migration и две соседние retained карты. Отдельный
смешанный сценарий создаёт новую страницу с реальной ссылкой SEC, сохраняет её,
проверяет physical slot/material reopen, затем height-only save опубликованного
рабочего профиля снова оставляет RES прежним. Undo/Redo проверяют использование
новой страницы. Намеренно stale source вызывает один ожидаемый диагностический
диалог и сохраняет destination. Синтетический document test проверяет failure
rollback, unknown bytes/trailer и native hash-chain lookup старых/нового членов.

18 SKIP — необязательные проверки внешних/private fixtures, переменные для них
не задавались; это не runtime PASS. В одном новом atomic-backup test Qt сообщил
о невозможности автоматически убрать собственную временную папку с readonly
backup; функциональные assertions прошли. Живые данные/backup владельца не тронуты.

Первоначальные сборки не прошли из-за ошибок нового harness (`corners`, Qt `slots`
macro); они исправлены до проверенного build. Первый headless `--version` запуск
из корня завершился DLL initialization error; повтор с явным Qt/MinGW PATH и
собственной рабочей папкой прошёл. Первые native прогоны выявили ошибки настройки
собственного harness: подтверждение QMessageBox через done вместо click, полный
искусственный atlas512 и отсутствие global family. После исправления harness
финальный исходный/смешанный сценарии прошли. Первые UI попытки безопасно
прервались с exit2: текущий native clipboard format49163 нельзя восстановить;
synthetic writes не выполнялись. Финальный UI на точном EXE прошёл с capture и
restore буфера. Эти ранние попытки не считаются успешными проверками.

Доказательства в owned `build-tests-122-height-res/`: `height-res-save-retry5-native/`,
`documents-native/tests.txt`, `ui-widget-retryfinal-native/`,
`terrain-mob-height-retryfinal-native/`, `scenario-native/`, `world-snap-native/`,
`version-retryfinal-native/`, `stage-version-result.json`, `staging-result.json`,
`finalize.log`, `validate-package.log`. Shadow output `build-release-122-height-res/`
не является пользовательской установкой. Финализированная staging-папка после
проверки не запускалась.

Окончательный EXE SHA256:
`7fab42e44507988dfd76c216c36dd86786c47bc2b1d62171d1874dc23f8bb3dc`.
Staging `build-staging-122-height-res-handoff`, manifest SHA256
`8e5c34949af54511d8511977f265ba2b1453eb82de66174eb424a609fbe0926c`.

## Передача и границы

Следующий шаг — scoped-коммит main и guarded обновление единственной постоянной
установки `artifacts/EI-Mapper-Ataman-2-first-test` с сохранением пользовательских
файлов. Результат установки будет дописан после readback.

Визуальный редактор/игра не запускались вместо владельца. Приёмка на исходной
карте и native игре остаётся отдельной: изменить только рельеф, сохранить,
проверить неизменную дату RES, открыть MPR заново; затем повторить с новой
покраской и общей кистью. Ресурсная целостность и автоматические tests не
подменяют эту проверку. Родительские цели переходов и перенос Zone19 сохраняются.
