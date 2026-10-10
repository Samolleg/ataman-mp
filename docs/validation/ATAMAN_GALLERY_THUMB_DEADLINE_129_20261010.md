# EI-GALLERY-THUMB-DEADLINE-129 — 2026-10-10

## Результат и границы передачи

Исправлена программная причина задержки первых видимых миниатюр: GUI повторно
измерял все 7165 подписей текстур при LayoutRequest. Порог завершения 300 мс
сохранён и сделан строгим; дополнительно проверяется разрыв обслуживания GUI
менее 100 мс. Конечный model_gallery_check: 20/20 PASS, dark-theme: 70 PASS / 0 FAIL.
Полный --ui-widget-check BLOCKED: защитный helper отказался сохранять текущий
формат буфера 49470, exit2 до запуска child и любых записей. Это не PASS.
Реализация готова к ревью, но весь пункт129 до полного UI-прогона не закрыт.

Поручение владельца через Claude 2026-10-10. Старт: main,
`33d5c29f6cbca25483dcc589a0efa43ad3db42a7`. Перед отчётом main продвинут сопровождающим
до `4454e3b9a8eebeb0fe82f4842509ab952a0969af`; между SHA изменены только посторонние
документы/прототип, затронутые исходники программы не менялись. Эти изменения
не являются результатом данной задачи. AGENTS.md SHA256 при входе и перед передачей:
`F4B7FDD5EA82FCDF43D002D23D64EC36BE853650F054EDF70376527CD6432EC3`.
Рабочая VERSION остаётся128, планируемая версия129. VERSION, ROADMAP, CHANGELOG,
Readme, translations и постоянная установка не изменялись. Git использован только
для чтения; новых веток/worktree, подагентов, сети и установки зависимостей не было.
Навыков evil-islands-modding/private-engine-reference нет; приватный движок
не исследовался. Использованы обычный код проекта и разрешённое чтение RES.

## Причина и изменения

1. `modelGalleryTexture` имел стандартный AdjustToContentsOnFirstShow. Установка
   minimumContentsLength(20) сама по себе не ограничивает измерение содержимого.
   В скрытом native harness переопределение QApplication::notify локализовало
   задержку до QEvent::LayoutRequest (type76) на CModelGallery: 32 события,
   min253.771 / median267.990 / max319.695 мс. В одном предварительном запуске
   событие заняло324.155 мс, provider calls=0. Разрывы GUI видны и при session
   cache, когда provider calls=0. Значит, это не стоимость RES, рендера, запуска
   worker, глобальной QtConcurrent очереди или GUI-среза8 мс.
2. В production combo установлен AdjustToMinimumContentsLengthWithIcon.
   Расширяемое editable поле, все строки/разделы и MatchContains completer
   сохранены. Измерение 7165 строк больше не входит в отложенную раскладку.
   Функциональный тест проверяет содержимое последней строки и completer,
   точные binding/sections, manual persistence, Shift/Ctrl/reverse/keyboard,
   несколько worker, stale texture/filter/scroll, закрытие при N заданиях.
3. `ready()` теперь требует непустой viewport и правильный текущий cache key
   каждой видимой иконки, включая смену текстуры. Deadline использует
   PreciseTimer; PASS дополнительно требует фактического elapsed<limit.
   Baseline coarse timer позволял завершённым проверкам вернуть PASS после300 мс.
   Тот же нормальный event loop измеряет first-ready и максимальный GUI-gap;
   GUI-gap>=100 мс отвергается независимо от завершения. Это более строгий
   критерий, не увеличение порога. Он применён и к соседним ready-проверкам500 мс.
4. Возврат к кэшированной области восстанавливает точное прежнее значение
   scrollbar. scrollToItem(EnsureVisible) после раскладки может выровнять строку
   иначе и показать дополнительные некэшированные элементы; прежнее ожидание
   «ноль новых provider calls» тогда было неверно. Сценарий действительно
   проверяет возврат к тому же viewport, а новый viewport отдельно требует calls.
5. Связанный shutdown: статический QThreadPool создаётся раньше локального
   static QRegularExpression в validKey. На раннем отказе оставшийся disk worker
   может продолжить работу после разрушения regex. Один baseline-app дал
   exit0xC0000374 и сообщение invalid QRegularExpression, один native probe —
   exit0xC0000005. Добавлен qAddPostRoutine: при разрушении QApplication дождаться
   pool до разрушения статических disk helpers. Закрытие диалога не ждёт.
   Контроль ниже воспроизводит зависание старого кода и нормальное завершение нового.
6. Опциональная `EI_GALLERY_TIMING_TRACE=1` выводит GUI dispatch/texture resolution,
   worker queue/read/provider/write и GUI delivery. В обычном запуске trace
   не выводится. Дисковый формат, ключ, бюджет, RES provider и параллелизм не менялись.

## Методика и распределение

Release Qt5.12.12 / matching MinGW7.3, native Windows platform. Собственные
каталоги build-release-129-thumbs и build-tests-129-thumbs. Qmake/make вызваны
непосредственно: штатный wrapper запускает lrelease с записью translations/*.qm
вне владения. Сборки128 для проверки; runtime/упаковка/установка сопровождающему.
Основная сборка создана с нуля; попытка скопировать старые objects не состоялась
из-за ошибочного относительного пути, копирования в чужой каталог не было.
Одна промежуточная сборка завершиласьexit2 (отсутствовал include QCoreApplication
для qAddPostRoutine); исправлено, конечная сборка exit0.

Серии before-app и after-final: по20 отдельных процессов одной скрытой программы,
497 моделей /7165 текстур, одинаковый viewport1056x380, по5 исходно видимых плиток.
Namespace включает новый QTemporaryDir на запуск: первый показ — настоящий miss
memory/disk, reopening в том же процессе — session hit. Общий производный cache
находится только у собственного EXE в build-tests-129-thumbs/cache. Файлы кэша
между процессами накапливаются, поэтому дисковые хвосты не исключены из результата.
Каждый запуск получает атомарный mkdir build-locks/ui-tests.lock, owner.txt,
finally-release; ожидание чужого lock20с, максимум30мин. Частота probe5мс;
first-ready — момент наблюдения на GUI, не время фактического первого paint.
Скрытая native проверка не доказывает визуальную приёмку владельца.
P95 ниже — nearest rank, при n20 это19-е значение.

| Метрика, мс | До, min / median / p95 / max | После, min / median / p95 / max |
|---|---:|---:|
| Выход из cold ready(), n20 |301 /310 /317 /322|85 /104.5 /139 /199|
| Первая наблюдённая готовая плитка |294 /303 /320 /320, n14|75 /92.5 /129 /189, n20|
| Cold GUI-gap |260 /274 /299 /303|6 /6 /8 /8|
| Session reopening |265 /270 /281 /281, n6|4 /5 /6 /8, n20|
| Changed-resource namespace |279 /280 /310 /310, n6|19 /20 /55 /260, n20|

Важно: baseline cold не завершился в14/20 случаях. Первая строка для FAIL —
время выхода по deadline, не время полной загрузки. В6 случаях первая плитка
до deadline не наблюдалась. В1 случае probe вообще не получил управление;
для gap использован elapsed до deadline, а не ошибочный raw gap=0.
Все6 baseline exit0 вернули complete после300мс и новым строгим критериям
не соответствуют. Итог before-app:6 exit0 /13 exit1 /1 crash; after-final:20 exit0.
Сырые данные, включая FAIL, не отбрасывались: distributions.json и папки серий.

Дополнительный native notify probe до фикса:20 запусков,6 exit0 /13 exit1 /
1 crash; cold exit299–328мс, median310мс; LayoutRequest доказательства выше.
Промежуточная серия after-app до shutdown-дополнения:20/20 PASS, cold54–89мс,
median67мс, GUI-gap5–7мс. Более высокая финальная медиана связана с наполнением
дискового каталога; trace показывает операции read/write, а не GUI-паузу.
Первые20 пробников без QT_FORCE_STDERR_LOGGING не дали доступных timing logs,
для количественных выводов не использованы. Один промежуточный after-sizing
отказал на прежнем неверном ожидании возврата viewport; исправление пункта4
подтверждено всей финальной серией.

Финальный stage trace (включает все диалоги группы, synthetic sleeps исключены
только из fast-provider статистики):

| Этап | median / p95 / max, мс |
|---|---:|
| GUI dispatch |0.069 /0.507 /8.734|
| texture/key resolution в dispatch |0.019 /0.045 /0.193|
| worker scheduling |0.015 /0.753 /9.307|
| disk read, включая ожидание mutex |0.282 /7.747 /109.420|
| fast synthetic provider |0.025 /0.048 /0.258|
| disk write, включая prune/mutex |2.498 /90.292 /184.180|
| GUI result delivery |0.065 /0.150 /0.242|

Первый write в процессе пересчитывает directory accounting; при заполненном
каталоге scan/prune и общий disk mutex могут задержать worker. После серий
в своём cache1276 записей /129491032 байта, ниже128MiB. Это остаточная стоимость
кэширования: на финальных входах она не нарушила300мс и не блокировала GUI.
Её нельзя назвать «мгновенной», и абсолютная гарантия300мс при любой нагрузке
ОС/диска не заявляется. Она не объясняет baseline LayoutRequest с calls0 и
250+мс session hit. По полученным цифрам увеличивать порог300мс оснований нет.

## Реальные RES — отдельные холодный старт и fill

Разрешённые исходные RES читались напрямую без изменений; приватная копия
catalog/models/textures хранится в собственном build-tests-129-thumbs/real-fixture.
Эквиваленты из128:36 unmo и71 stbu, по8 видимых плиток. Baseline — скопированный
benchmark128; новый benchmark связан с final objects. Для каждого EXE свой
пустой app-relative cache. Это точечные измерения, не распределение20 запусков.

| Показ |128 fill / total, мс|Исправление fill / total, мс|Исправление max GUI gap|
|---|---:|---:|---:|
|unmo cold,8 calls|353.107 /639.609|99.212 /382.762|31|
|stbu cold,8 calls|283.902 /304.959|32.208 /46.008|8|
|unmo session,0 calls|243.460 /259.560|5.634 /19.806|5|
|stbu session,0 calls|246.714 /263.519|6.935 /21.597|6|
|unmo disk warm, новый процесс,0 calls|не измерено|32.396 /314.030|28|
|stbu disk warm, новый процесс,0 calls|не измерено|12.169 /28.968|7|

fill начинается после show; total включает ctor/первое скрытое native окно,
но не предшествующую подготовку процесса. В первом новом unmo процессе ctor
175–180мс и show103–106мс остаются отдельной стоимостью старта; warm disk не
означает total<300мс. Native harness не воспроизводит жест владельца, остальные
категории/renderer/gameplay не проверялись. Это не изменение критериев test:
его synthetic queue по-прежнему требует fill<300мс и теперь GUI-gap<100мс.

## Shutdown-контроль и остальные проверки

Контроль открывает скрытую галерею с непустым disk namespace, дожидается начала
600мс provider, удаляет диалог и выходит. Старый model_gallery.cpp из HEAD
(его code совпадает со стартовой базой) связан с тем же harness и остальными
objects. Старые3/3 не завершились; собственные процессы принудительно остановлены
после53.125 /9.031 /9.797с (exit4294967295), это HANG/FAIL, не PASS.
Новый код:3/3 exit0, process total1.391 /1.235 /1.235с, close диалога1–2мс.
Финальный основной checker тоже оставляет600мс disk worker при закрытии,
в20/20 случаях процесс завершается нормально. Код ждёт только при завершении
QApplication, не на закрытии пользовательской галереи.

| Проверка final EXE | Результат |
|---|---|
|model_gallery_check|20 функциональных групп PASS /0 FAIL; без SKIP|
|dark-theme|70 PASS /0 FAIL, exit0|
|Полный UI под image guard127|BLOCKED, exit2;0 assertions выполнено; child не запущен|
|Рабочая версия|EI Mapper Endurance Edition128, exit0|
|Сборка final / diff whitespace|exit0 /PASS|
|Shutdown helper|3 PASS /0 FAIL|

Guard: `Native clipboard format 49470 is not preservable; check aborted before writes.`
Пользователю предложено сначала сохранить содержимое и подготовить обычный текст
в буфере; до ответа защита не обходится. Полный UI не заменялся standalone тестами.
Интерактивный редактор/игра не открывались, clipboard-данные не раскрывались.
При настройке runner один запуск ошибочно сделан без lock; он завершился на
ошибке Qt platform plugin до создания тестовой галереи, его данные не входят
в статистику. Все успешные диагностические серии/прогоны выше были под lock.

Final EXE SHA256:
`E228CD76FD868B62EC563A1C9F9C93C30AFC48405ED16B24EC2160CFA21BBD52`.
Source hashes:
- model_gallery.cpp: `E5D24F09CFB545AD5B5E6AC3C9DAADA149FE69FF4437C7CF27D98BA547F399E9`.
- model_gallery_check.cpp: `9C1E9A108169E3A5697150AFE9C0003B200E4700166DE2D18082EAC05332A85E`.

## Evidence и точное владение для коммита

Engineering outputs, не для Git:
`build-tests-129-thumbs/{run-series.py,distributions.json,layout-events.json}`,
`before-app-01..20`, `after-final-01..20`, `before-measured-01..20`,
`after-app-01..20`, `dark-final-01`, `full-final-01`, `version-final-01`,
`shutdown-before-01..03`, `shutdown-after-01..03`,
`real-before-cold-01`, `real-after-cold-01`, `real-after-disk-warm-01`,
build logs и private fixtures. Сборка: `build-release-129-thumbs/release`.

Точный список файлов для scoped-коммита сопровождающего:
- изменён `source/ui/model_gallery.cpp`;
- изменён `source/app/model_gallery_check.cpp`;
- новый `docs/validation/ATAMAN_GALLERY_THUMB_DEADLINE_129_20261010.md`.

model_gallery.h и gallery_thumbnail_cache.cpp/.h не изменены. Другие рабочие
изменения не включать в этот scoped-коммит. Номер, общие документы, финальную
сборку и постоянную установку ведёт сопровождающий.

## Предлагаемый текст ROADMAP (не записан в общую карту)

### EI-GALLERY-THUMB-DEADLINE-129 — дедлайн видимых миниатюр

- Поручение2026-10-10 через Claude; база main33d5c29f6cbca25483dcc589a0efa43ad3db42a7,
  рабочая128, планируемая129. Установить причину timing120/127/128, распределение
  >=20 до/после, исправление, gallery/dark/full UI; без записи общих файлов,
  Git/установки/живых данных. Реализация ограничена gallery и checker.
- Подтверждено: LayoutRequest измерял7165 texture labels253.771–319.695мс.
  Bounded combo sizing убрал GUI-паузу;300мс сохранены, deadline строгий,
  current-key readiness и GUI-gap<100мс. Финальные20/20 gallery PASS:
  cold85–199мс median104.5, GUI-gap6–8мс; session4–8мс. Dark70/0.
  Реальные unmo/stbu cold fill353/284→99/32мс; disk warm32/12мс.
  Shutdown исправлен: baseline3 HANG→3 PASS, close1–2мс.
  Evidence: docs/validation/ATAMAN_GALLERY_THUMB_DEADLINE_129_20261010.md.
- Статус: реализация/распределение готовы; full UI BLOCKED guard127 на текущем
  формате буфера49470, до child/записей. Следующий шаг — безопасный буфер,
  полный UI на final EXE E228CD76..., затем scoped-коммит, VERSION/CHANGELOG,
  финальная сборка/установка сопровождающим. До полного UI пункт129 не закрывать.

## Предлагаемая запись CHANGELOG (не записана)

### 129 — EI-GALLERY-THUMB-DEADLINE-129

- Галерея больше не измеряет тысячи текстур при GUI-раскладке; первые видимые
  миниатюры не ждут этой паузы. Весь список текстур и completer сохранены.
- 300мс deadline сохранён и сделан строгим: проверяются текущие ключи иконок и
  GUI-gap<100мс; cached-scroll возвращается к той же области. Завершение
  приложения дожидается disk workers до разрушения их статических helpers;
  закрытие диалога остаётся неблокирующим.
- Проверено:20/20 gallery (cold85–199мс, GUI-gap6–8мс), dark70/0,
  shutdown3/3; реальные RES cold/session/disk warm. Full UI пока BLOCKED guard
  формата49470; перед финальной передачей обязателен отдельный успешный прогон.
  Рабочая сборка128, общий номер129 назначает сопровождающий. Evidence: отчёт выше.

## Передача 130

EI-FINALIZE-130, поручение владельца через Claude от 2026-10-10. Ветка main,
снимок `git archive 32a2c512247c533a4a9482e6307e19182b47adbc`, VERSION=130.
AGENTS.md актуален: SHA256
`F4B7FDD5EA82FCDF43D002D23D64EC36BE853650F054EDF70376527CD6432EC3`.
Сборка из `build-release-130-final/src`, fresh shadow `build-release-130-final`,
EXE `build-release-130-final/release/EIMapperEndurance.exe`.
1783 файла снимка сверены с архивом; checkout EOL Git сохранены, lrelease
сгенерировал qm только внутри снимка. Qt 5.12.12 / MinGW7.3 x64;
оба x86 runtime-компонента пересобраны. Первый внешний PowerShell вызов дал
NativeCommandError на stderr qmake; отдельный wrapper receipt подтвердил exit0
и завершение runtime build (это не повторный функциональный тест).

EXE SHA256 (tested = staged = installed):
`9EBC89BD809042F4AA97884F38CCB42C124C8CEDF4991F4FD8D2F5812867D421`.

| Проверка | Результат |
| --- | --- |
| Release / --version | exit0 / EI Mapper Endurance Edition 130 |
| Полный --ui-widget-check под image guard | **BLOCKED**, child не запускался; не PASS |
| --scenario-layer-check | 50 PASS / 0 FAIL / 0 SKIP, exit0 |
| --dark-theme-check | 70 PASS / 0 FAIL / 0 SKIP, exit0 |
| --model-gallery-check | 1 полный functional suite PASS, 497 моделей / 7165 текстур, exit0 |
| world_brush_130_integration (регресс129) | 8/8 отдельных случаев; Qt24 PASS (с init/cleanup) / 0 FAIL / 0 SKIP, exit0 |
| gallery_thumbnail_cache_tests | Qt7 PASS / 0 FAIL / 0 SKIP, exit0 |
| Staging --version / identity | 130, хеш EXE совпадает с tested |
| Finalizer + internal validator | PASS, 127 managed payload files |
| Установка и сохранность | 127/127 hashes PASS; 22433/22433 user files unchanged |

Перед UI выполнена только нативная инвентаризация форматов буфера:
49161, 2, **49470**, 49678, 49679, 49171, 8, 17. Unicode text13 отсутствовал.
Формат49470 ранее отвергнут guard как несохраняемый; по текущему поручению
буфер не изменялся, fixture не ставился и child не запускался. Это BLOCKED,
а не успешный прогон. Владельцу сообщено; требуется простой текст в буфере,
затем обязательный полный UI на том же EXE под `--guard-image-child`.
На все выполненные UI/native проверки захватывался `build-locks/ui-tests.lock`
через mkdir с owner.txt EI-FINALIZE-130; lock освобождался в finally.

Новый staging: `build-staging-130-final-handoff`; finalizer применён только
к этой собственной новой папке. Runtime Qt/MinGW и прочие неизменные payloads
взяты по проверенному manifest чистого staging129; новые EXE/runtime/VERSION/notes
из текущего снимка. Русский перевод уже встроен через qrc; временно добавленный
внешний qm удалён из собственного staging, затем finalizer/validator повторён
после этого изменения. После финализации staging не запускался и не менялся.
Manifest SHA256: `52A8E058031EBE82154C3FB4F01A9F9011996BAD7690440E8268B1BA4ED442C9`.

Постоянная установка: `artifacts/EI-Mapper-Ataman-2-first-test`, **129 → 130**.
Перед записью выполнены process guard и сверка всех старых managed hashes.
Старый EXE SHA256: `B70F56E4F8D0B2E187E5469923644613CDA81B0CC41BCE4E07D0B767D9245CA5`.
Старый manifest: `6D08B53C84547CF6DDF95B72F170CD0DCA8F68B7CBD310223E183E4EAAFE5631`.
Заменены 6 managed файлов: CHANGELOG.md, EIMapperEndurance.exe, Readme.md,
VERSION, runtime/EIMapperEnduranceJmvBridge.plg,
runtime/EIMapperEnduranceRuntimeHost.exe; manifest заменён последним.
Каждый backup проверен по SHA256 до атомарной замены. Rollback129:
`artifacts/rollback-EI-Mapper-Ataman-v129-to-130-20261010`.
Старые rollback127/128 не удалены. Настройки, карты, сохранения, логи и кэши
сохранены: хеши всех22433 unlisted user files до/после равны.
Receipts: `build-tests-130-final/{snapshot,release-result,staging-result,install-plan,install-result,rollback-hashes,user-files-before,user-files-after,process-guard}.json`;
результаты и stdout/stderr каждого теста — в одноимённых подпапках этого каталога.

**Ограничение передачи:** установка130 и независимые проверки завершены;
полная функциональная приёмка EI-GALLERY-THUMB-DEADLINE-130 остаётся открытой
до обязательного полного UI. Для129 исходный мазок Swamp zone3obr и ручная
смена карты остаются ручной приёмкой; synthetic/native не заменяют её.
Следующий шаг: сохраняемый буфер → полный UI под guard и lock → обновление
этого результата. Живые игровые/модовые данные, карты и Endurance не изменялись;
интерактивного запуска, сети, установки зависимостей и push не было.

## Полный UI после снятия блокера

- Полный UI сопровождающего 2026-10-10 на установленной 130 (EXE SHA256 9EBC89BD809042F4AA97884F38CCB42C124C8CEDF4991F4FD8D2F5812867D421), после замены буфера владельцем на текст: --ui-widget-check под guard изображения 171 PASS / 0 FAIL, буфер восстановлен (build-tests-128-gallery/baseline-image-retry-claude130; поле finalExeSha256 runner относится к сборке 128 и не описывает проверенный EXE). Блокер полного UI для 129/130 снят.
