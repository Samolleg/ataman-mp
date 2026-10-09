# CF_BITMAP в защите буфера — Ataman127

Задача `EI-CLIPBOARD-GUARD-CF-BITMAP-127`, выбор владельца2026-10-09,
поручение через сопровождающего Claude. База `main`
`de0a47b398938dca464f0968b608b87731bb00bb`,126→127.
Изменены только verified_clipboard.cpp, его тесты/тестовый .pro и документы
версии/этот отчёт. Сеть, игра, Endurance, живые карты и мод не изменялись;
Qt/MinGW из существующего toolchain Endurance только использован для сборки.
Подагенты, новые чаты, ветки и worktree не создавались.

## Причина и реализация

NativeClipboardState::capture отвергал любой FORMATETC без TYMED_HGLOBAL.
CF_BITMAP/TYMED_GDI не несёт блока HGLOBAL, но Windows может получить bitmap
из DIB/DIBV5. Отказ126 подтверждён на синтетическом исходном случае под
внешним guard: CF_BITMAP+DIB → exit2 до тестов, «Native clipboard format2
is not preservable». Изображение и исходный буфер внешнего guard восстановлены.

Теперь GDI-зависимости проверяются после захвата всех HGLOBAL, независимо
от порядка перечисления OLE. Полный несжатый DIB/DIBV5 проверяется по header,
planes/dimensions/bpp/compression, длине таблицы/масок и пиксельных строк;
неизвестный/неполный источник не разрешает пропуск GDI. Исходный порядок
HGLOBAL сохраняется, включая DIBV5/DIB. После восстановления проверяются
ID/приоритет/размер и каждый байт всех захваченных HGLOBAL, доступность и
получение ожидаемого CF_BITMAP. Readback перенесён после CloseClipboard и
повторного открытия: до закрытия Windows ещё не завершает синтез GDI.

Отклонение от рекомендованного подхода для CF_PALETTE основано на проверке:
на этом desktop индексированный DIB дал CF_BITMAP, но CF_PALETTE не был
доступен (IsClipboardFormatAvailable=0). Поэтому при наличии полного DIB
с таблицей цветов guard дополнительно сохраняет фактические PALETTEENTRY,
создаёт палитру до EmptyClipboard, передаёт её Windows и проверяет все записи
после восстановления. Сам opaque GDI handle не сериализуется. Системные
GDI API разрешаются через QLibrary, без правки application .pro или новой
зависимости. Тестовый .pro связывает gdi32 для собственных native fixtures.
Без DIB-источника CF_PALETTE по-прежнему вызывает отказ до записей.

Метафайлы не синтезируются из raster DIB; CF_ENHMETAFILE/CF_METAFILEPICT
по-прежнему отвергаются, включая сочетание метафайла с DIB. Встроенный OLE
объект/TYMED_ISTORAGE также не теряется ради прохождения проверки.

## Буфер владельца и воспроизведение

Первое действие с буфером было read-only EnumClipboardFormats/OLE EnumFormatEtc,
без EmptyClipboard/SetClipboardData. Найдены13 native IDs; OLE перечислил
Embed Source/TYMED_ISTORAGE, Object Descriptor/HGLOBAL, CF_METAFILEPICT,
CF_DIB и PNG/image/png HGLOBAL. В native списке также были CF_BITMAP/DIBV5.
Baseline126 безопасно отказал на Embed Source49163. Этот объект невозможно
восстановить из DIB; менять буфер инструментами для обхода отказа не стали.

Позже read-only перечитывание обнаружило текст и браузерные бинарные форматы,
без встроенного объекта. Каждый native тестовый процесс сперва успешно
захватывал текущий буфер guard, и лишь затем создавал собственные fixtures.
Для baseline126 и финального127 внешняя защита установила изображение
BITMAP→DIB; DIB и DIBV5 до/после дочернего UI сравнивались побайтно,
CF_BITMAP оставался доступен. Внутренний guard127 также проверил все свои
HGLOBAL/приоритеты при восстановлении. После дочернего прогона внешняя
защита вернула исходное содержимое владельца с native readback PASS.
Содержимое буфера не печаталось и не сохранялось в файлы.

## Проверки

Qt5.12.12/MinGW7.3, штатная Windows platform; скрытые виджеты, без
интерактивного запуска редактора/игры. Конечный EXE SHA256:
`34D26E98BBF77F287EEF8FFF4448D5DA0EE4733AE8CD612704B189FEE9FC5F45`.

| Проверка | Результат |
| --- | --- |
| Baseline126, CF_BITMAP+DIB | ожидаемый отказ exit2 на format2,0 UI тестов;2 PASS внешнего восстановления |
| Финальные native clipboard tests | 17 PASS / 0 FAIL / 0 SKIP; дополнительно PASS возврата буфера владельца, exit0 |
| Полный --ui-widget-check на конечном127 при изображении | 169 PASS / 0 FAIL, exit0; ещё2 PASS независимого восстановления |
| Предыдущие проверки120 | текст/HTML/URL/image alpha/binary/native alias/empty,100 Copy, lock/retry/reentrancy/Cut/Undo PASS |
| Новые native случаи | BITMAP перед DIB, индексированный DIB+палитра и точные entries, отказ палитры без DIB, отказ метафайла с DIB PASS |
| Дедлайн миниатюр300мс | в одном полном финальном прогоне не воспроизвёлся; стабильность всех последующих запусков не утверждается |
| --scenario-layer-check, конечный127 | 48 PASS / 2 FAIL, exit1 |
| Scenario на неизменном126 | те же48 PASS / 2 FAIL, exit1 |
| Свежий Release127, оба runtime | PASS; конечная повторная сборка после изменения guard PASS |
| raw --version / staged --version / raw=stage EXE и runtime hashes | PASS,127 |
| staging finalizer со встроенным distribution validator | PASS,127 managed; удалений0 |
| Установленные managed файлы/manifest и сохранность user files | PASS,127 managed+manifest,21008 пользовательских файлов byte-identical |
| Чужой QM | исходные байты восстановлены после каждой сборки; SHA256 AB06B0CC2247832C97FC5DCD654E32854C2712ADD23EC17378E451F743C7F1E3 |
| diff whitespace ограниченных исходников | PASS |
| Scoped-коммит main | НЕДОСТУПЕН: git add не может создать .git/index.lock, Permission denied |

Scenario FAIL относятся к невозможности создать test backup в
`C:/Users/samol/AppData/Local/qttest/EI Mapper Endurance/backups/...` и
связанному неожиданному предупреждению при загрузке синтетического MOB.
Окружение разрешает запись только в workspace/temp; код backup_manager и
scenario не менялся. Та же ошибка подтверждена на126. Это не PASS,
обязательный успешный scenario-регресс остаётся неподтверждённым.

Промежуточные отказы не скрыты: исходное чтение Embedded OLE, ранний readback
GDI до CloseClipboard, предположение о синтезе палитры, служебные Qt5 QProcess
pipes (CreateFile/Access denied), application link без gdi32. Тестовый holder
теперь использует файл-маркер готовности с прежними таймингами/проверками и
forwarded channels; UI child пишет stdout/stderr в собственные временные файлы.
Таймаут/ошибка дочернего процесса продолжают приводить к FAIL и возврату
буфера внешним guard. Окончательный native17/0 и UI169/0 получены после этих
исправлений; исходные clipboard/UI assertions не ослаблены.

Протоколы: `build-tests-127-clipboard/`: `native-retry-dynamic-final/`,
`baseline-image/`, `ui-image/`, `scenario/`, `scenario-baseline/`,
`release-build.{out,err,json}`, `finalize.log`, `staging-result.json`,
`install-result.json`, `user-files-{before,after}.json`, `retirement-result.json`.
Engineering artifacts не включаются в Git/пакет.

## Передача и открытые ограничения

Новая staging-папка `build-staging-127-clipboard-handoff` создана с нуля из
127 проверенных managed файлов предыдущего чистого staging126, с заменой
программы, runtime и текущих документов. Staged127/version/hash проверены
до finalizer. После финализации пакет не запускался и не изменялся.
Manifest SHA256:
`63FB1A0AE4E504C2D2300163A3EEDAE04F884EF697D898AFE2CED5CD797174C3`.

Постоянная `artifacts/EI-Mapper-Ataman-2-first-test` обновлена126→127.
Заменены6 программных файлов; manifest последним. Перед записью проверены
процессы, прежний manifest/все управляемые hashes и точные backup bytes.
Все127 установленных managed hashes, manifest и21008 unlisted файлов
проверены. Настройки, логи, кэш, карты и сохранения не очищались; finalizer
на постоянной установке не запускался. ZIP/публикация не выполнялись.

Сохранена одна завершённая rollback126:
`artifacts/rollback-EI-Mapper-Ataman-v126-to-127-20261009`.
Предыдущая positively identified rollback125 удалена только после проверки
новой установки, provenance, семи прежних backup файлов, hashes и отсутствия
посторонних файлов/reparse points.

Реализация, исходный clipboard-регресс, полный UI и установка готовы.
Полное закрытие критериев задачи остаётся открытым из-за scenario FAIL и
недоступного коммита: .git в данном окружении доступен только для чтения,
git add отказал на index.lock; staged изменений нет. Вместо изменения
защищённого Git подготовлен `build-tests-127-clipboard/scoped-review.patch`
из HEAD только для своих файлов/строк ROADMAP. Точный HEAD остаётся указанной
базой; SHA нового коммита отсутствует. Прежние чужие изменения сохранены.
Следующий шаг: применить проверенный scoped-патч к index и сделать коммит в
main в окружении с записью в .git; успешный scenario подтвердить в окружении
с доступным изолированным Qt test backup. Ручная приёмка владельцем отдельно.

## Дополнение сопровождающего (Claude), 2026-10-09

- Scenario повторён вне песочницы Codex (запись в изолированный
  `LOCALAPPDATA/qttest` доступна) тем же runner на конечном EXE SHA256
  `34D26E98BBF77F287EEF8FFF4448D5DA0EE4733AE8CD612704B189FEE9FC5F45`:
  **50 PASS / 0 FAIL**, exit0, `build-tests-127-clipboard/scenario-retry-claude/`.
  Оба прежних FAIL — следствие ограничения песочницы, не дефект127.
- Установленный EXE совпадает с конечным по SHA256, `--version` = 127.
- Число пользовательских файлов установки 21008 против 21240 у126 объяснено
  работой программы между установками: смена кэша персонажей (−1577/+1345,
  только `cache/`) и новые журналы `logs/workflow-*`; до/после установки127
  набор идентичен.
- Scoped-коммит main выполнен сопровождающим после проверки diff; чужие
  незакоммиченные изменения не включены.
