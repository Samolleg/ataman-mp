# Win32 DIB-источник для OLE BITMAP — 2026-10-10

Задача `EI-CLIPBOARD-GUARD-OLE-BITMAP-SOURCE`, поручение владельца через
сопровождающего Claude. База `main`, VERSION **131**, HEAD
`7515a3fb4d30f3601d700ea7ebbafbbcfec6d71c`.
AGENTS.md сверён с действующим файлом; SHA256
`F4B7FDD5EA82FCDF43D002D23D64EC36BE853650F054EDF70376527CD6432EC3`.
Прочитаны отчёты CF_BITMAP127 и STREAM131, CONTRIBUTING и договор сборок.
Последнее поручение ограничивает изменения core/test/build/evidence: общие
ROADMAP/CHANGELOG/VERSION/Readme, Git index/коммиты и установка не изменялись.
Параллельные незавершённые RES-reader-drain файлы сохранены; они не включены
в проверяемый EXE. Сеть, установка зависимостей, подагенты, интерактивный
редактор, игра/мод/карты не использовались.

## Фактическая причина на живом буфере

До первой синтетической записи read-only Win32/OLE inventory подтвердил:

| Win32 | OLE EnumFormatEtc(DATADIR_GET) |
| --- | --- |
| 49161 DataObject | отсутствует |
| 2 CF_BITMAP | 2, TYMED_GDI=16 |
| 49470 PNG | 49470, TYMED_ISTREAM=4 |
| 49678 CanUploadToCloudClipboard | 49678, TYMED_HGLOBAL=1 |
| 49679 CanIncludeInClipboardHistory | 49679, TYMED_HGLOBAL=1 |
| 49171 Ole Private Data | отсутствует |
| 8 CF_DIB | отсутствует |
| 17 CF_DIBV5 | отсутствует |

Все четыре OLE FORMATETC: aspect=1, lindex=-1. Гипотеза подтверждена:
OLE перечисляет форматы исходного объекта, а Windows предоставляет DIB/DIBV5
через Win32 синтез. Capture131 собирает только OLE byte payloads и не видит
источника CF_BITMAP. Неизменённый прежний native executable на этом живом
буфере дал exit2 до тестов/записей, точное сообщение:
`Native clipboard format 2 has no preservable synthesis source; check aborted before writes.`
Протоколы: `build-tests-clipdib/live-baseline/` и `live-baseline-refusal/`.

## Исправление

После чтения всех OLE payloads guard проверяет отложенные CF_BITMAP/CF_PALETTE.
Если пригодного источника нет, он открывает Win32 clipboard только для чтения,
находит доступные недостающие CF_DIB/CF_DIBV5 через
IsClipboardFormatAvailable/GetClipboardData и копирует их HGLOBAL bytes.
Каждый новый источник проходит существующий canSynthesize: допустимые header,
размеры, planes/bpp/compression, маски/таблица цветов и полные строки пикселей.
Лимиты **128 МиБ на формат / 256 МиБ суммарно** проверяются до копирования.
Недоступное открытие допускает прежний bounded retry конструктора; неполный,
невалидный или чрезмерный источник вызывает отказ до записей.

Оба источника сохраняются в их относительном Win32-порядке. На данном
скриншоте это DIB затем DIBV5; фиксированная перестановка не навязывается.
Прежний restore материализует blocks до EmptyClipboard, записывает сохранённые
bytes, закрывает/reopens clipboard и сравнивает все bytes/приоритеты,
IsClipboardFormatAvailable и GetClipboardData для CF_BITMAP. Палитра сохраняет
прежние реальные PALETTEENTRY; требуется DIB с таблицей цветов. Отказы для
метафайлов, ISTORAGE и форматов без полного источника не ослаблены.

Начальный внешний guard нового native набора успешно захватил именно живой
OLE-скриншот до установки fixtures и вернул все **5 исходных byte payloads**
(PNG, два cloud/history markers, DIB, DIBV5) с native readback и CF_BITMAP.
Затем полный live UI проверял тот же скриншот после материализованного
восстановления. В этом наборе **6 публичных byte-форматов**: прежние пять плюс
monitor exclusion DWORD0, добавляемый непустым guard как в131. DataObject/Ole
Private Data не replay: это прежний договор131, а не пользовательские bytes.
Идентичность COM-объекта/transport IDs не утверждается.

Байты владельца хранились только в памяти guard/readback; содержимое не
печаталось и не сохранялось в файлы. Все записи/очистки выполнялись под guard.

## Изолированная сборка и проверки

Снимок: `git archive HEAD` → `build-release-clipdib/head.tar` →
`build-release-clipdib/src`. Наложены только четыре разрешённых файла
verified_clipboard.{cpp,h}, verified_clipboard_tests.{cpp,pro}.
1804 tracked-файла снимка сверены с archive: других отличий, отсутствующих
или дополнительных файлов нет; overlays совпадают с рабочими файлами.
Test .pro не имеет Git diff (различие archive/overlay — окончания строк).
Windows tar неверно декодировал имя одного документа DOCX; имя исправлено
в собственной snapshot-папке после подтверждения идентичных bytes. Этот
документ не участвует в компиляции. Receipt: `provenance.json`.

Применены существующие Qt5.12.12/MinGW7.3, штатная Windows platform,
скрытые виджеты, desktop OpenGL. Wrapper из snapshot собрал приложение
в `build-release-clipdib/app` и оба runtime из тех же snapshot-исходников.
Native tests собраны в `build-tests-clipdib/native-build` из snapshot.
Все native/UI/inventory запуски — только под атомарным mkdir
`build-locks/ui-tests.lock`, owner.txt с task/mode/PID и finally-удалением.

Конечный проверенный EXE:
`build-release-clipdib/app/release/EIMapperEndurance.exe`

SHA256: **537723797CACD3E990ED80532A11B216CEE62B2688B3B5ED9AC639A115A9A8E7**.

| Проверка | Результат | Протокол в build-tests-clipdib |
| --- | --- | --- |
| Read-only первоначальный live inventory | 8 Win32 / 4 OLE, exit0 | live-baseline |
| Baseline131 на живом OLE-скриншоте | ожидаемый отказ2 до записей; 0 native tests | live-baseline-refusal |
| Native: прежние25 + новый OLE bitmap без OLE DIB | **26 PASS / 0 FAIL / 0 SKIP**, exit0; ещё PASS возврата владельца | native-contention-recheck/results.txt |
| Полный UI, живой скриншот | **169 UI PASS / 0 FAIL**, exit0; **4 внешних PASS**, всего173 | ui-live-final |
| Полный UI, PNG(ISTREAM)+BITMAP(GDI), без DIB в OLE | **169 UI PASS / 0 FAIL**, exit0; **4 внешних PASS**, всего173 | ui-ole-bitmap |
| Полный UI, ISTREAM+DIB/DIBV5+markers, регресс131 | **169 UI PASS / 0 FAIL**, exit0; **3 внешних PASS**, всего172 | ui-stream-131 |
| Полный UI, native BITMAP→DIB, регресс127 | **169 UI PASS / 0 FAIL**, exit0; **2 внешних PASS**, всего171 | ui-image-127 |
| Fresh Release + оба runtime | exit0 | release |
| --version | EI Mapper Endurance Edition131, exit0 | version |
| Whitespace своего diff / чистота snapshot | PASS | git diff --check / provenance.json |

Четыре внешних live/OLE-bitmap assertions: read-only baseline публичных
byte-форматов; точные DIB/DIBV5 bytes и доступность bitmap; все6 byte payloads
и CF_BITMAP/PNG; возврат буфера владельца. Новый native test отдельно доказывает
отсутствие DIB/DIBV5 в OLE enumeration, BITMAP/GDI и PNG-only ISTREAM,
неизменность clipboard sequence при capture, точные восстановленные payloads
и получение CF_BITMAP. OLE-bitmap UI inventory также подтверждает исходную
топологию: 9 native IDs / 5 OLE formats, без DIB/DIBV5 в OLE.

Промежуточный native-first: **25 PASS / 1 FAIL / 0 SKIP**, не PASS.
Старый restoreLiteralNativeAlias разово не прочитал native format50000.
Отдельный адресный alias-прогон:3/0/0, затем полный26/0/0 на неизменном core;
assertions и дедлайны не ослаблялись. Точная причина разового отказа не
установлена; transient contention остаётся гипотезой. Он не воспроизвёлся
в диагностическом/полном повторе. После native PASS менялся только внешний
child image comparator: он теперь реально проверяет DIB bytes также в
live/stream/OLE-bitmap режимах; относящийся live UI повторён, все три
синтетических UI выполнены с усиленным comparator. Core/native test cases
после26/0/0 не менялись.

Изолированная сборка — engineering artifact для интеграции сопровождающим,
не обновление постоянной установки. VERSION131 сохранён по поручению;
следующий номер/коммит/интеграция назначаются сопровождающим. Ручная вставка
в Paint/браузер/Office, проверка Windows history UI не выполнялись и здесь
не объявляются PASS. Все критерии данного clipboard-поручения подтверждены;
посторонние suites не повторялись без относящегося риска.

## Файлы для scoped-коммита

- source/core/verified_clipboard.cpp
- source/core/verified_clipboard.h
- tests/verified_clipboard_tests.cpp
- docs/validation/ATAMAN_CLIPBOARD_OLE_BITMAP_SOURCE_20261010.md

verified_clipboard_tests.pro не изменён; build-папки/протоколы не включать.
Чужие RES-reader-drain изменения и общие документы не включать автоматически.

## Готовый текст ROADMAP для сопровождающего

```markdown
## EI-CLIPBOARD-GUARD-OLE-BITMAP-SOURCE — Win32 DIB для OLE BITMAP

- Поручение владельца через Claude2026-10-10; база main7515a3fb4d30f3601d700ea7ebbafbbcfec6d71c, VERSION131. Core/test/evidence, изолированные snapshot-сборки; Git/общие документы/установка — сопровождающему.
- Причина подтверждена на живом скриншоте:8 Win32 formats против4 OLE; DIB/DIBV5 синтезируются Windows и отсутствуют в OLE enumeration. При нехватке raster GDI source guard read-only захватывает Win32 DIB bytes с прежними completeness/128MiB/256MiB limits и приоритетом; metafile/ISTORAGE fail-closed сохранён.
- Проверки: native26/0/0; полный UI169/0 на живом скриншоте (+4 внешних PASS) и OLE PNG/ISTREAM+BITMAP/GDI без OLE DIB (+4); регрессы131169+3 и127169+2 PASS. Все сохранённые bytes совпали, CF_BITMAP/PNG доступны, буфер владельца возвращён. Fresh Release/оба runtime/version131/snapshot1804 PASS.
- Статус: исправление и согласованные проверки готовы к scoped-интеграции; номер следующей версии/коммит назначает сопровождающий. Общие файлы и установка не менялись. Evidence: docs/validation/ATAMAN_CLIPBOARD_OLE_BITMAP_SOURCE_20261010.md. EXE SHA256537723797CACD3E990ED80532A11B216CEE62B2688B3B5ED9AC639A115A9A8E7.
```

## Готовый текст CHANGELOG для сопровождающего

Добавить под назначенным сопровождающим номером версии:

```markdown
- EI-CLIPBOARD-GUARD-OLE-BITMAP-SOURCE: исправлен отказ защиты буфера на реальном OLE-скриншоте PNG/ISTREAM+BITMAP/GDI. Если OLE не перечисляет DIB/DIBV5, guard сохраняет предоставленные Win32 источники с прежними проверками полноты/размера и восстанавливает их bytes/приоритет; метафайлы и ISTORAGE без источника по-прежнему отклоняются до записей. Проверено: native26/0/0; полный UI169/0 на живом скриншоте и новой OLE-синтетике, регрессы127/131; bytes сохранены, CF_BITMAP и PNG доступны. Evidence: docs/validation/ATAMAN_CLIPBOARD_OLE_BITMAP_SOURCE_20261010.md.
```


## Передача 132

Задача EI-FINALIZE-132, поручение владельца через Claude 2026-10-10.
База и HEAD main: `6bd5e44e382fd201f322fc8ec3e3b96dff282151`.
AGENTS.md проверен до работы и перед отчётом: SHA256
`F4B7FDD5EA82FCDF43D002D23D64EC36BE853650F054EDF70376527CD6432EC3`.
Правила передачи docs/TEST_BUILD_HANDOFF.md прочитаны; исключения поручения:
обновление установки разрешено, старые rollback сохраняются, Git и общие
документы не изменяются. ROADMAP текущей задачи прочитан; готовый текст ниже.

### Сборка и происхождение

`git archive` указанного коммита → `build-release-132-final/source.tar` →
`build-release-132-final/src`. Архив выдавал CRLF для 1594 текстовых файлов:
только в снимке они приведены к точным blob bytes коммита. Все **1805** файлов
проверены по Git blob SHA1: **0 отличий / 0 пропусков / 0 дополнительных**,
VERSION=132. Повторная проверка после сборки также 1805/0/0/0; штатный lrelease
писал QM в снимок и воспроизвёл уже отслеживаемые bytes без изменения дерева.
Незакоммиченные файлы параллельной задачи в сборку не включены.
Receipts: `build-tests-132-final/provenance.json`, `provenance-after-build.json`.

Свежий штатный Release wrapper из снимка, Qt5.12.12/MinGW7.3,
shadow: `build-release-132-final/release`; выход:
`build-release-132-final/release/release/EIMapperEndurance.exe`.
Приложение и оба x86 runtime собраны из того же снимка, exit0 (121.08 с).
`--version`: **EI Mapper Endurance Edition 132**, exit0.
EXE SHA256 (сборка = staging = установка):
**DF44E7E6BD840FAD308525A0C1E08B367805846A8F049086CC9E9CEC6A1FEA06**.

### Регрессы финального EXE

Все UI/native запуски под атомарным mkdir `build-locks/ui-tests.lock`,
owner.txt с EI-FINALIZE-132/PID/временем и finally-удалением собственной
блокировки. Ожидание занятой блокировки:20 с, предел30 мин. В конце lock
отсутствует. Обычная Windows platform, скрытые виджеты, desktop OpenGL.
Содержимое буфера не печаталось и не сохранялось в файлы; все его записи
выполнялись внутри guard. Полный live UI выполнен до native fixtures.

| Проверка | Результат | Протокол build-tests-132-final |
| --- | --- | --- |
| Полный --ui-widget-check на текущем живом изображении под --guard-live-child | **169 UI PASS / 0 FAIL**, ещё **4 внешних PASS**, exit0, 8.97 с | ui-live/0.out, result.json |
| --scenario-layer-check | **50 PASS / 0 FAIL**, exit0, 1.36 с | scenario |
| --dark-theme-check | **70 PASS / 0 FAIL**, exit0, 1.16 с | dark-theme |
| Native clipboard | **26 PASS / 0 FAIL / 0 SKIP**, exit0, 4.84 с; дополнительный PASS возврата буфера | native/results.txt, 0.out |
| Fresh Release + оба runtime | exit0 | release |
| Built / staged --version | 132 / 132, exit0 | version/0.out, staged-version.out |

Внешний live guard подтвердил read-only capture, точные image HGLOBAL bytes
и CF_BITMAP, все **6 публичных byte-форматов** и доступность PNG, восстановление
буфера владельца. Native также подтвердил возврат исходного буфера.
Исходная причина исправления описана выше: OLE не перечисляет Win32-синтезированные
DIB/DIBV5 при PNG/ISTREAM + BITMAP/GDI. Финальный132 сохраняет эти источники.

### Staging, manifest и установка

Создана ранее не существовавшая `build-staging-132-final` из снимка и свежих
EXE/runtime; ресурсы icons/heightmaps, лицензии и текущие notes взяты из снимка.
Qt/MinGW runtime развёрнут windeployqt --release --compiler-runtime.
Raw/staged EXE hashes равны, staged --version132 проверен до finalizer.
Первый finalizer отказал из-за отсутствующей **Qt5Concurrent.dll**: это FAIL,
не PASS. DLL добавлена из того же Qt kit; повторный finalizer на изменённом
собственном staging дал **PASS**, включая validate_distribution.ps1 и полный
manifest **126 файлов**, удалено0 stale/private/runtime files.
Протоколы `finalizer.out` (первичный отказ), `finalizer-final.out` (PASS).
После финализации пакет не запускался и не изменялся. ZIP не запрашивался.

Постоянный путь: `artifacts/EI-Mapper-Ataman-2-first-test`, **131 → 132**.
Host Get-CimInstance Win32_Process: **0** процессов с ExecutablePath внутри
этой установки. До записи проверены все **127** old-manifest hashes; конфликтов
managed bytes и коллизий с unlisted user files нет. Изменено9 managed paths:
CHANGELOG.md, EIMapperEndurance.exe, heightmaps/README.md, LICENSE.txt, NOTICE.md,
Readme.md, runtime/EIMapperEnduranceJmvBridge.plg,
runtime/EIMapperEnduranceRuntimeHost.exe, VERSION. Старый managed TRAVEL_EDITOR.md,
отсутствующий в новом manifest, удалён только после проверки старого hash и backup.
Текстовые отличия части лицензий/README — точные LF bytes снимка коммита.

Rollback131: `artifacts/rollback-131-to-132-final` — **10 предыдущих managed
файлов** (9 заменённых +1 удалённый) и прежний SHA256SUMS.txt; каждый backup
побайтно проверен до замен. Это частичная техническая копия для возврата,
не отдельная установка и не полный старый пакет. Старые rollback не удалялись.
Замены через sibling temp + os.replace; новый manifest заменён последним.
Все **126** установленных managed hashes совпали с новым manifest,
**0 hash errors**, manifest совпал со staging; установленный VERSION=132.
**23 062** пользовательских unlisted файла, включая настройки/логи/кэши,
сохранены: одинаковый состав и SHA256 до/после, **0 ошибок**.
Receipts: `pre-install.json`, `install.json`, `user-hashes-before.json`
в build-tests-132-final (локальные данные, не для Git).
Финализатор на постоянной установке не запускался.

### Границы и передача сопровождающему

Интерактивный редактор/игра не открывались; ручная вставка в Paint/браузер/Office
и Windows history UI не проверялись. Автотесты/manifest не заменяют эту приёмку.
Сеть, зависимости, ветки/worktree, git add/commit и записи в .git не применялись.
Изменения исходной программы отсутствуют. Единственный файл для коммита задачи:
`docs/validation/ATAMAN_CLIPBOARD_OLE_BITMAP_SOURCE_20261010.md`
(только дополнение раздела «Передача132»). Build/staging/rollback/runtime receipts
не включать в Git. Общие документы и чужие изменения сохранены.

Готовое дополнение ROADMAP (в текущий пункт132, сопровождающему):

```markdown
- EI-FINALIZE-132: передача завершена из снимка main6bd5e44e382fd201f322fc8ec3e3b96dff282151, без параллельных правок. Fresh Release132/оба runtime; UI169+4/0 на живом изображении, scenario50/0, dark70/0, native26/0/0; буфер восстановлен. Finalizer/manifest126 PASS. Постоянная установка131→132:126 managed hashes/0 ошибок;23062 user files сохранены; rollback artifacts/rollback-131-to-132-final. Evidence: docs/validation/ATAMAN_CLIPBOARD_OLE_BITMAP_SOURCE_20261010.md, раздел Передача132. Следующий шаг: ручная приёмка владельцем; исходная автоматическая передача завершена.
```

Готовое дополнение CHANGELOG132 (сопровождающему):

```markdown
- EI-FINALIZE-132: свежая Release132 из коммита6bd5e44e382fd201f322fc8ec3e3b96dff282151 проверена на живом изображении: UI169/0 (+4 проверки сохранности), scenario50/0, dark70/0, native26/0/0. Постоянная установка обновлена131→132 с rollback и сохранением23062 пользовательских файлов; manifest126/0 ошибок. Evidence: docs/validation/ATAMAN_CLIPBOARD_OLE_BITMAP_SOURCE_20261010.md, Передача132.
```
