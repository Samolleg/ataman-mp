# OLE ISTREAM в защите буфера — Ataman131

Задача `EI-CLIPBOARD-GUARD-STREAM-131`, поручение владельца через Claude
2026-10-10 «Делаем 131». База main `8f3128085d3bd8db5641d0598854945b70f1888c`,
VERSION130→131. AGENTS.md прочитан, SHA256
`F4B7FDD5EA82FCDF43D002D23D64EC36BE853650F054EDF70376527CD6432EC3`.
Изменены только согласованные core/test/version/document paths. Сеть,
зависимости, подагенты, другие checkout, игра/мод и живые карты не использовались
для записи; интерактивный редактор не запускался. Существующий toolchain
Qt5.12.12/MinGW7.3 прочитан и использован без изменений.

## Причина и границы подтверждения

Capture130 допускает TYMED_HGLOBAL и растровые GDI с полным DIB-источником;
FORMATETC PNG/TYMED_ISTREAM отвергается как «Native clipboard format49470
is not preservable». Неизменённый EXE130
`9EBC89BD809042F4AA97884F38CCB42C124C8CEDF4991F4FD8D2F5812867D421`
воспроизвёл этот точный отказ на собственном IDataObject: PNG принимает только
ISTREAM, DIB/DIBV5 и cloud/history markers — HGLOBAL. Baseline exit2,
0 выполненных UI assertions; все3 внешние проверки сохранности PASS.
Синтетика установлена OleSetClipboard только после успешного захвата
пользовательского буфера внешним guard; он же вернул исходный буфер.

Для первоначального скриншота владельца medium не измерен в этой сессии:
он уже заменён владельцем браузерным текстом. Поэтому ISTREAM как причина
именно того скриншота остаётся гипотезой; дефект130 и совпадающий отказ
на PNG-only ISTREAM подтверждены экспериментально.

## Чтение и восстановление

Guard131 читает IStream с нулевого смещения, сохраняя прежнюю позицию потока.
Seek/Stat/Read должны завершаться корректно, длина должна совпасть с Stat;
ошибки, усечённое содержимое и непрочитываемые полностью источники отвергаются.
Лимит128 МиБ на формат применяется до выделения и на каждом chunk64 КиБ;
лимит256 МиБ — на суммарное содержимое снимка. Предпочтителен HGLOBAL;
при отказе GetData и объявленном ISTREAM применяется потоковый путь.
Дубликаты одного ID принимаются только с одинаковыми байтами. ISTORAGE,
метафайлы, target-device/lindex/aspect без поддерживаемого представления
продолжают вызывать отказ до любых синтетических записей.

Восстановление материализует HGLOBAL до EmptyClipboard, сохраняет приоритет
пользовательских форматов и сравнивает ID/длину/все байты после CloseClipboard.
Opaque GDI handles не сериализуются: BITMAP из полного DIB, PALETTE через
сохранённые PALETTEENTRY, как127. Пустой поток имеет нулевой блок; обычная
явно пустая проверенная запись сохраняет пустой MIME.

Выбор HGLOBAL основан на Windows/OLE эксперименте, без сетевых предположений:
native GetClipboardData материализует исходный ISTREAM в байты HGLOBAL;
после нативного SetClipboardData и выхода UI child OleGetClipboard/GetData
возвращает идентичные PNG байты как для TYMED_HGLOBAL, так и TYMED_ISTREAM.
QImage декодирует PNG, включая alpha; DIB/DIBV5 и все marker payloads также
побайтно совпадают. Специальный восстановленный IDataObject с ограниченным
сроком жизни не нужен: данные сохраняются после завершения тестового процесса.
Интерактивная вставка в Paint/браузер/Office не выполнялась; проверены оба
системных потребительских API и декодирование PNG.

## OLE metadata, cloud и история

Синтетический OLE источник даёт native DataObject49161 и Ole Private Data49171,
но EnumFormatEtc(DATADIR_GET) их не перечисляет. Guard130/127 уже сохраняли
публичные OLE payloads, а не эти транспортные данные. Guard131 сохраняет этот
подход: не replay устаревших process/COM metadata. После материализованного
восстановления этих внутренних ID нет, Windows предоставляет новый wrapper.
Native/OLE inventories записаны без пользовательских байтов в протокол UI.

CanUploadToCloudClipboard и CanIncludeInClipboardHistory — обычные byte
payloads; fixture содержит DWORD1, восстановление проверяет те же4 байта.
Существующий ExcludeClipboardContentFromMonitorProcessing также сохраняется
побайтно. Если его нет, непустое восстановление добавляет DWORD0 этого формата:
его присутствие предназначено для исключения содержимого из Windows monitor
processing/history/cloud. Это осознанное дополнение технического ID, а не
обещание идентичности OLE-внутреннего native набора. Пользовательские форматы,
их байты, значения исходных cloud/history markers и относительный приоритет
сохраняются. На synthetic fixture exclusion уже присутствует, все публичные
форматы и байты до/после совпадают.

Проверенные временные Copy через этот модуль под живым guard также несут
exclusion; обычный Copy вне guard не меняется. Явно пустая запись исключение
не добавляет, поскольку содержимого для истории нет. Guard не удаляет прежние
элементы Win+V и не изменяет настройки истории. Фактический список Win+V и
состояние cloud service интерактивно не проверены. Read-only history API
подтвердил отсутствие новых элементов на этой конфигурации (ниже). Нельзя обещать
контроль сторонних clipboard consumers или других native writers.

## Проверки и промежуточные результаты

Все UI/native прогоны используют mkdir `build-locks/ui-tests.lock`, owner.txt
с ID/режимом/PID и finally-удаление. Штатная Windows Qt platform, скрытые
виджеты, desktop OpenGL; таймаут/сбой/отказ не объявляются PASS. Байты владельца
не печатались и не сохранялись в файлы. Read-only inventory текущего буфера
показывает браузерный HTML/Unicode/text/Chromium internal formats.

- Первый link native fixture потребовал существующую системную uuid library;
  подключена только в тестовом .pro. Промежуточный fixture-прогон18/2 выявил
  неполный тестовый ISTORAGE источник и transient OpenClipboard lock. Источник
  теперь создаёт реальный IStorage; fixture использует существующий bounded
  openForCheck. Исходные assertions не ослаблены. Следующий native20/0 PASS.
- Дополнительные отказы not-seekable/read-failure/truncated/over128MiB:
  native23/0/0 PASS. С empty-write регрессом:24/0/0 PASS.
- Первый UI131 на stream-source:170 PASS/2 FAIL (один итоговый lifecycle FAIL),
  причина — exclusion сделал явно пустую запись непустой для строгого readback.
  PNG/DIB/marker bytes и возврат владельца —3 PASS. Причина исправлена в
  согласованном core, добавлен empty-write тест; новая свежая сборка обязательна.
- Один ручной запуск native inventory без Qt plugin environment не стартовал;
  не является проверкой/PASS. Штатный runner с explicit plugin environment
  выполнил read-only inventory exit0.

- Дополнительный marker test выявил, что Qt при записи регистрирует буквальное
  MIME имя: Windows read-alias создаёт отдельный ID. Два промежуточных native
  24/1 не подтвердили native marker bytes. Исправлен буквальный canonical name,
  а bounded readback/fixture OleSetClipboard учитывают transient contention
  (после этой правки промежуточные23/2 — lock при установке/чтении fixtures,
  затем25/0). Сохраняются переданные marker bytes, включая DWORD1. Новые растровые
  fixtures также помечены canonical exclusion в своей native транзакции.
  Assertions/дедлайны не ослаблены. Последний25/0 уже на окончательных fixtures.

## Конечные функциональные проверки

Конечный EXE SHA256:
`39707F231B5FCAFD8A292EF4CA58BB9F499E4B4A4F66B03939A15724F643DBD3`.
Общий source core не менялся после этих прогонов. Свежая shadow Release131
создана в `build-release-131-stream-final`; после относящихся core исправлений
повторно собрана с тем же VERSION, оба runtime вновь собраны из source.

| Проверка | Результат | Протокол в build-tests-131-stream |
| --- | --- | --- |
| Native, все17 прежних +8 новых | **25 PASS/0 FAIL/0 SKIP**, exit0; ещё PASS возврата владельца | native-final-pass/results.txt |
| UI PNG-only ISTREAM+DIB/DIBV5+3 markers | **169 UI PASS/0 FAIL**, exit0; ещё3 внешних PASS (итого172) | ui-stream-done/0.out, 0.err |
| UI BITMAP→DIB, регресс127 | **169 UI PASS/0 FAIL**, exit0; ещё2 внешних PASS (итого171) | ui-image-done/0.out, 0.err |
| Scenario | **50 PASS/0 FAIL**, exit0 | scenario-done |
| Dark theme | **70 PASS/0 FAIL**, exit0 | dark-done |
| Model gallery | **PASS**, exit0;497 models/7165 textures, functional/Shift/thumbnail/cache/close diagnostics PASS | gallery-done/0.err |
| Fresh Release131 + оба runtime | **PASS**, exit0 | release-final, release-done |
| Raw --version | **131**, exit0 | version-done |
| translations/*.qm vs HEAD | **byte-identical**, git diff --exit-code0 | Git verification |
| git diff --check | **PASS** | Git verification |

Синтетический source inventory до UI: PNG49470 tymed4 (ISTREAM only),
DIB/DIBV5/cloud/history/exclusion tymed1. После UI: те же payloads tymed5
(HGLOBAL|ISTREAM), BITMAP tymed16. Native DataObject/Ole Private Data отсутствуют
после восстановления, остальные все6 payload bytes совпали; PNG декодируется
по обоим medium, BITMAP доступен. Финальные native25 включают точный native
ID/байты уже заданного marker1 и автоматически добавленного marker0.

Протоколы находятся в `build-tests-131-stream/`; engineering артефакты не
включаются в Git или пакет. Посторонние untracked файлы в
tools/terrain_bake_prototype появились после начальной проверки clean tree;
они не читались, не изменялись и не включаются в scoped-коммит.
## Передача131

Реализация+документы закоммичены в main:
`37740d26ea6b60f18add6b0ddc8e597dad65294d`.
Author/committer Codex <codex@localhost> заданы только окружением команды;
финальный trailer Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>.
Глобальный git config не изменялся, push не выполнялся. Последующая запись
передачи меняет только этот отчёт и собственный пункт ROADMAP; её SHA хранится
в финальном ответе и `build-tests-131-stream/handoff-commit.json`.

Fresh staging `build-staging-131-stream-handoff` создана заново из проверенных
127 managed payloads чистого staging130 с заменой editor/runtime/version/readme
и единственного актуального changelist131. Raw/staged EXE и оба runtime hashes
совпали; staged --version131 подтверждён до finalizer. Finalizer и внутренний
distribution validator PASS, удалений0, coverage127/127.
После finalizer пакет не запускался и не изменялся. Manifest SHA256:
`DC081F3154732FAC2336F3EB89404DC2CBD67954A67394D57FB682350107B3A9`.

Постоянная `artifacts/EI-Mapper-Ataman-2-first-test` обновлена130→131.
До записи проверены отсутствие editor/runtime процессов, все старые managed
hashes и manifest130, все staging hashes. Verified rollback130 содержит точные
прежние6 файлов и manifest:
`artifacts/rollback-EI-Mapper-Ataman-v130-to-131-20261010`.
Файлы заменены через собственные временные файлы и atomic os.replace,
manifest последним. Итог:127/127 установленных managed hashes и manifest PASS,
**22453/22453 unlisted user files byte-identical**. Настройки, карты, saves,
логи и кэши сохранены, finalizer на постоянной установке не запускался.
Все прежние rollback127/128/129 сохранены. EXE установки совпадает с tested
конечным EXE SHA25639707F231B5FCAFD8A292EF4CA58BB9F499E4B4A4F66B03939A15724F643DBD3.
ZIP не запрошен, публикация/сеть запрещены и не выполнялись.

Отдельная проверка реальной Windows history: IsHistoryEnabled=true;
GetHistoryItemsAsync=Success. До и через1500мс после полного stream-guard UI
число25→25 и весь набор history item IDs одинаков. Новых элементов/дубликатов
после synthetic PNG, временных UI Copy и возврата владельца не обнаружено.
Read-only API запрашивал только ID, содержимое history не читалось и ID не
сохранялись/не печатались. Дополнительный полный UI нужен для ответа именно
на незакрытый вопрос history:169 UI PASS+3 внешних PASS, exit0, тот же конечный
EXE; тесты не повторялись вместо этой проверки. Это наблюдение на текущей
Windows, не гарантия для стороннего clipboard manager или иных cloud services.

Receipts: `staging-result.json`, `finalizer.out`, `stage-version.out`,
`process-guard.json`, `install-plan.json`, `rollback-hashes.json`,
`install-result.json`, `user-files-{before,after}.json`, `history-result.json`,
`history-probe/` в build-tests-131-stream. UI lock снят, QM byte-identical HEAD.
Все заданные автоматические критерии и передача выполнены. Осталось
подтвердить medium исходного скриншота и реальную вставку в Paint/браузер/Office
при ручной приёмке владельцем; эти приложения не открывались автоматически.
