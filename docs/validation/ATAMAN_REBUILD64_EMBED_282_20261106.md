# EI-REBUILD64-RENDERER-EMBED-282 — этап 2, 2026-11-06

**IMPLEMENTED / STAGED, с незакрытым критерием строгой GL pixel regression.** Встроенный вид и функциональные проверки работают. Полный PASS задания не заявляется: нулевое отличие GL281/282 не получено; нестабильность воспроизводится и между запусками самой281. Визуальная приёмка нового renderer владельцем отдельно. STOP BEFORE INSTALLATION, без push.

## Идентичность и границы

Вход: main, HEAD `61b0e24b928ae510a5ff022b10e763de287f27b1`, VERSION281; следующий свободный282. AGENTS.md прочитан и повторно сверен, SHA256 `2A2F58EF34F63D6C5D67D165D0B809B046D0A519181119D22EA8FDD8781FE3B7`. Прочитаны CONTRIBUTING, договоры данных/сборки и оба renderer study/probe отчёта. Профильных навыков evil-islands-modding/private-engine-reference нет; работа ограничена разрешёнными renderer/adapters, кодом маппера и чтением данных, приватные классы движка не переносились.

W = `work/rebuild64-renderer-embed-282-20261106/`. Материал `work/rebuild64-renderer-probe-20261105/` только читался. Только эта машина: Windows x64, RTX4070SUPER, D3D11 FL11.0, driver32.0.16.2101. Все тестовые HWND принадлежат изолированным процессам и скрыты; итоговые проверки зафиксировали0 visible HWND. GL — собственный FBO, новый вид — readback собственного accepted GPU target. Desktop/computer-use/скриншоты владельца не использовались.

Установленные/старые мапперы не запускались. Установка, owner AppData/settings, ProgramData locks, игры и Base-карты не менялись. Qt known-folder resolver и lock store в стенде перенаправлены в W до MainWindow; это не только APPDATA env. Autosave/MCP discovery выключены. Чужие процессы, мосты, checkout и ветки не менялись. Clipboard, subst/junction не использовались; финальный `subst` пуст.

## Реализация

Workspace теперь содержит QWidget/QStackedLayout с тем же CView и native HWND surface. Настройки → Вид карты → OpenGL / Rebuild64 (просмотр), переключение без перезапуска. QSettings `ui/renderer` сохраняет выбор, default OpenGL. Решение о будущем основном режиме сохранено: отдельный этап после инструментов.

QLibrary загружает DLL только после выбора Rebuild64 при открытой карте. Начальный OpenGL не создаёт renderer worker и не загружает DLL: проверено также GetModuleHandle. После использования обратное переключение приостанавливает тёплый backend, сохраняя кэши; OpenGL после первого переключения уже не означает отсутствие его памяти.

ABI2: фиксированные C-структуры и таблица функций, единственный экспорт `ataman_renderer_get_api`. Qt/STL/исключения через границу не идут, MinGW с DLL не линкуется. QLibrary/D3D/destruction живут на одном worker; QWidget и сборка memory snapshot — UI thread.

MPR строится чистой транзакцией из канонического состояния; до отложенной инициализации редактора берутся оригинальные байты CLandscape в памяти. Видимые MOB сериализуются в память без Save. Адаптер принимает immutable memory MPR/MOB; с диска читает ресурсы моделей/текстур. Исправлены map-local atlas priority и перевод generic mount0 в vendor AllKinds3. Полная замена снимка сбрасывает прежнюю source publication: игровой same-name MOB reuse здесь не подходит. Атласы явно передают Renderer::retire_surface до удаления.

Общая CCamera, GL→D3D depth matrix, часы/region/cave, свет/fog из renderEnvironment. W/A/S/D/Q/E/стрелки/Shift, middle orbit и wheel используют тот же controller; инструменты не передаются. Проверены движение W и точное сохранение matrix при возврате в OpenGL.

Source pin EIRebuild64 `81dbcdd29b41baeb346f167c6dfb511dff05f295`; upstream/data pin `b1ffc736d4792eb36f3528a8d8ee2c358da80d54`. В Git только наш host/widget/ABI, lock, рецепт и7 reviewable patches, без vendor sources. Рецепт читает Git objects, извлекает архивы в собственный work и проверяет hashes патчей. Исправлен вложенный git-archive случай: repository prefix позволял git apply молча пропускать пути. Теперь GIT_CEILING_DIRECTORIES ограничивает поиск репозитория; no-NGX результат проверяется отдельно.

DlssSdk.props и dlss_backend.cpp исключены; собственный no_ngx.cpp явно сообщает недоступность и не создаёт SDK worker/каталоги. SDK не линкуется. Dumpbin и состав подтверждают отсутствие NVIDIA/NGX/Streamline imports/runtime. Original LICENSE/NOTICE/PROVENANCE, component notices и credit **EID3D11 renderer by Ilufus** сохранены. Вопрос разрешения автора закрыт и не пересматривался.

## Готовность и шейдеры

Gate читает настоящее состояние: tile blend ready/rows, pending atlas copies, style serial/dirty, seen-ready, полный набор baked terrain cells, failed. RGB не используется. Native swapchain получает GPU copy только после готовности; resize front buffer отложен до такого кадра. До первой готовности виден OpenGL с сообщением. Пока новый документ готовится, остаётся прежний agreed frame.

Общего публичного presentable_generation API у pinned renderer нет. Временно используется read-only friend observer под lock/patch. Для устойчивого SDK от автора нужен token поколения с pending/ready/failed после shader/material/atlas/height/scene preparation и согласования GPU work. Наш observer не объявляется таким SDK-контрактом.

Прямой контроль финальной DLL: после принятого A отправлен B, первый настоящий pending дал mask43. Accepted readback во время pending **побайтно равен RGB прежнего A**; затем B стал ready. Evidence W/hold-receipt.json, images/hold-agreed-A.png и hold-pending-B.png. Во всех6 итоговых ready PNG HSV ROI владельца даёт0 purple; это дополнительная диагностика, не production gate.

Таблица638 shader programs встроена. Холодная сборка таблицы1371,5с,73335280B bytecode — работа сборки, не UI запуска. Повторный producer финального рецепта638 reused/0 compiled,1,7с. Runtime final:272 shader objects,0 compiled. Промежуточное холодное создание driver objects6,344с (shader preparation5806мс). Финальные запуски ниже уже с прогретым driver cache; полностью холодный driver cache финальной DLL не сбрасывался/не измерен.

## Парная картинка и поправка к этапу 1Б

Пары сохраняют исходные пиксели, добавлены только подписи:

- [Village: GL281 / Rebuild64-282](../../work/rebuild64-renderer-embed-282-20261106/pairs/zone1-village-GL281-Rebuild64-282.png).
- [Coast: GL281 / Rebuild64-282](../../work/rebuild64-renderer-embed-282-20261106/pairs/zone1-coast-GL281-Rebuild64-282.png).
- [Memory baseline / object-only / height+object](../../work/rebuild64-renderer-embed-282-20261106/pairs/zone1-memory-edits.png).

Обе стороны1280×720, VFOV45°, near1/far2000,12:40. Village eye(240,110,175),target(240,171,10); coast eye(90,22,52),target(90,140,6). Одинаково: freeze time0, остановлен water overlay clock, units скрыты, particle visuals выключены. Native camera update дождался нового кадра.

1. **Полосы реки.** Вывод1Б «опубликованные пары не содержат маркера» отозван. Старый RGB-фильтр R>100/B>100/G<45 возвращает0 на явно полосатом edited PNG: он не учитывает смешивание/освещение. В ROI x62–201/y42–403 независимый HSV-filter даёт997 purple pixels edited и0 before (другая методика, не число567 владельца). После70 кадров полосы исчезают без изменения данных. Terrain2 shader содержит именно переходный magenta для неготовых rows/atlas. Совокупность указывает на смешанный marker; контролируемая перекраска shader не делалась, поэтому происхождение не объявляется отдельно доказанным экспериментом. Независимо от этого **206,562/327,643мс не доказаны как ready latency** и не используются. Новый замер с настоящим gate: p50/p95/max=747,5/810/828мс,50 изменений высоты. Новый host полностью публикует memory snapshot; это явно другая граница end-to-end замера.

   Старая before/after-разница420549px/45,6325%, bbox почти весь кадр, meanabsRGB0,4081/255, >8 только9969px. В финальном хосте повторная публикация **того же** документа уже меняет74417px, meanabs0,0400, >8 только48px. Height+object меняют117418px, mean0,12664, >8=3628px. Широкий bbox не равен геометрической правке всей карты: заново строятся world-dependent GPU data и temporal histories. Разделить все составляющие старой разницы по этим данным нельзя. W/final-image-metrics.json и image-audit.json.

2. **Небо.** Sky включён; sky_clouds=0 даёт тёмный clear. GL рисует облачную текстуру цилиндра, owned — свой procedural sky. Coast наклонён вниз21,3° при VFOV45°: физический горизонт примерно21px от верха. GL clouds ниже него и owned horizon fill различаются. Matrix и геометрический ракурс совпадают; отсутствие map resource это не объясняет. Объяснение горизонтом основано на геометрии/коде; точное разложение цвета sky не установлено. Соответствие облаков GL **не исправлено**.

3. **Бледность/чёткость.** В пакет добавлены classes/units/region material maps, встроены Terrain2 masks: t119=128×128×60, heights281/281 baked. Исправлены material keys/data mounts нашего host. Контроль на той же DLL без external material data: village meanabs0,000884 (>8:6px), coast0,018689 (>8:163px). Контроль без57 mask resources: village0,000764 (>8:6px), coast0,028545 (>8:185px); renderer подтвердил terrain2_masks_omitted/bilinear fallback. Эти дефициты не объясняют общий вид деревьев/скал: world objects используют S2, не unit profile path, masks влияют на terrain transitions. Остаются другой свет/PBR, AA/filtering и procedural terrain; их вклады отдельно не измерены. Эквивалентность не достигнута. t90..t93=null в bake log не доказывает отсутствия heights: renderer намеренно снимает PS bindings перед UAV записью.

4. **Туман справа внизу.** Контроль fog_debug_component0..5 pinned renderer изолировал пятно в component3=mist/wisps. Component1 air haze и2 height fog такого пятна не дают. ROI RGB full≈81/113/98, mist≈83,6/116,7/99,1; без этой составляющей существенно темнее. [Лист0..5 слева направо, сверху вниз](../../work/rebuild64-renderer-embed-282-20261106/images/fog-components.png). Это Full fog water/swamp mist, не повреждение деревни. В нашем ABI hardcoded fog90/100 заменён GL fog180/620 и region/cave. Mist косметически не выключался.

В новом кадре отсутствуют: character appearance/анимации/экипировка, stst8/742, particles/spells, editor selection/grid/logic/help/gizmos и инструменты/picking. Проверенный вход — один активный MPR и видимые MOB; соседние MPR/world-workspace composite и gameplay/кампания не проверены. Zone1:1809 parsed objects,1808 static seeds,1 rejected seed. Village count801 omitted включает outside colour coverage и не означает801 потерянную модель.

## Проверки и замеры

| Сценарий | Результат / evidence в W |
|---|---|
| Production MainWindow/CView + финальная DLL | visual-final39 и close-final44 checks;0 visible HWND, normal shutdown |
| GL↔Rebuild64, shared camera | без рестарта; W input двигает shared controller; matrix при возврате exact |
| Memory edits | +6м у(240,176); object189(211,253;146,373;0)→(223,253;146,373;0); отдельный object-only PNG |
| A→B→A, close/reopen | zone1→zone2→zone1, затем unloadLand(false)→empty GL→reopen, одна DLL |
| Resize/DPR | 1280×720→1417×803; QT_SCALE_FACTOR1,25 даёт actual DPR2,5/3200×1800;33 checks |
| Missing DLL / bad PE / no D3D11 / init / device lost | по15 checks, автоматический GL/message; camera/hash/dirty сохранены |
| Ready hold | настоящий pending43, byte-identical previous accepted RGB |
| 50 updates / frames | final benchmark186 checks; benchmark.json,120 completed frame samples |
| Документы |4 fixture MPR/MOB после тестов SHA-identical входу, затем удалены |
| DLL/package |1 C export, SDK link/import/files отсутствуют; notices сохранены |

Финальная DLL, отдельные запуск1/запуск2: **1211/1252мс** до accepted view; тёплый возврат из GL709/704мс. UI heartbeat первого запуска77 callbacks/max gap41мс, второго121/max40мс. При DPR2,5 (одновременно шла сборка)8085мс,413 callbacks/max gap420мс. Это замеры этой машины, не обещание для других GPU.

50 updates: p50/p95/max=747,5/810/828мс включая readback.120 steady completed frame samples:2,8832/7,0019/8,3269мс. Frame time — CPU duration DLL frame/composite/present, не аппаратный GPU timer или полный UI frame. До retire_surface fix был линейный рост≈20MiB VRAM/update; после него такого роста нет.

| Состояние | Private bytes | Local VRAM bytes |
|---|---:|---:|
| Только GL, DLL не загружена |2797961216|1339682816|
| Только Renderer в C ABI процессе |1868210176|989999104|
| Оба backend resident, виден Renderer |4456460288|2505613312|
| До50 updates |4525256704|2492547072|
| После50 |4542472192|2492547072|
| После10с drain |4296818688|2318376960|

Private usage включает процесс/CPU кэши. VRAM — DXGI adapter local CurrentUsage процесса, не сумма конкретных textures. Отдельные процессы/сценарии нельзя строго вычитать.

**GL regression открыта.** Final282 vs281: village8px/coast7px из921600, max channel diff83/94. Два одинаково настроенных запуска самой281:80841px village (главным образом вода),5px coast. Внутри процесса readback стабилизируется до точного совпадения подряд. Причина межпроцессной воды и отдельных overlap pixels не установлена. Это **не PASS нулевого diff**, и не доказательство новой GL-регрессии. GL-код281 в Release взят из HEAD без dirty экспериментов владельца.

Изменения поведения при выбранном OpenGL: новый пункт переключателя; после использования Rebuild64 его тёплая память удерживается; при fallback появляется сообщение. Других намеренных изменений нет. Инструменты, панели, hotkeys, Save/форматы, MCP и uiScalePercent не менялись. Утверждение «картинка побайтно та же» ограничено сравнением выше. Удалено: **ничего**.

## Release, состав, сохранность

Release из git archive, Qt5.12.12/MinGW7.3 x64; DLL MSVCv143 x64:

- Mapper source tree `8ddbe1a7b631d37a8156ad8f41e2c1d29bbc5eb0`.
- Final renderer host/recipe tree `05b7b4aa95f40e63c04202397d017685d30d08b3`.
- Packaging docs tree `77d0f57d8faf0e45b33c25655b62dab3f89c5959`; последующие изменения только docs/стенд, production code тот же.
- Wrapper собрал mapper/MCP, но вызов runtime .cmd с long spaced path упал. Оба runtime target успешно пересобраны отдельно предусмотренными .cmd по8.3 path из того же archive. Это обход wrapper invocation, не копирование старых runtime.
- Raw/staged `--version`282. Первый staging offscreen probe завершился ошибкой из-за отсутствия qoffscreen (пакет содержит qwindows). Повтор со штатным packaged qwindows без внешнего Qt plugin path успешен.
- [Свежий staging](../../build-staging-282-rebuild64-view-20261106/). Finalizer/validator PASS:193 прежних managed paths→1364, **0 удалённых**. После финализации пакет не запускался/не менялся.
- 1171 добавленных файлов: DLL94969344B;1159 material data files1235843B;8 license/notice files35098B; msvcp140.dll557728B, vcruntime140.dll124544B, vcruntime140_1.dll49792B. [Каждый путь, размер и SHA](ATAMAN_REBUILD64_EMBED_282_20261106_ADDED_FILES.tsv). Сравнение с281 R2 staging, не живой установкой.
- DLL SHA256 `1B23982B215A1C6AE5C0D8990CD071FBE624C3BE50B83289BDDE40BE55F7F826`: проверенный process copy=staging. Mapper exe Release=staging; полные receipts/manifest в W.
- Все защищённые владельцем cpp/h/QM/.mcp.json/отчёт277 сохранили исходные SHA. Чужие строки ROADMAP сохранены и не включены в commit. Наши чистые view.h/landscape.h/main_window.cpp изменены отдельно; dirty view.cpp/landscape.cpp не менялись и не включены в archive.
- 4 private DejaVu fixture-копии удалены после SHA verification: W/fixture-cleanup-receipt.json. W/cleanup-plan.json описывает оставленные PNG, метрики, source archives/build intermediates. Probe и прочие work-папки не очищались.

## Оставшееся

Строгий0 diff GL281/282 не подтверждён; межпроцессная недетерминированность требует отдельного разбора. Визуальное соответствие неба/чёткости/тумана не достигнуто; причины и гипотезы выше. Общий upstream ready token пока заменён pinned observer; полностью холодный final driver cache не измерен. Characters/stst8 не входили в обязательный результат; другие GPU, gameplay, native-window smoke на видимом экране человека не проверялись. Все доступные изолированные этапы завершены, установка/push не выполняются.

## Попытка установки 2026-11-06 — BLOCKED BEFORE REPLACEMENT

Владелец принял282 и разрешил «ставь282». Проверены main и точный HEAD
`aab96e4994a45dedd9fe2f39996e331448923d89`; пересборка не выполнялась.
Цель: `artifacts/EI-Mapper-Ataman-2-first-test`, исходный пакет
`build-staging-282-rebuild64-view-20261106`, запрошенный откат
`artifacts/rollback280-to282-rebuild64-view-20261106`.

Вызван канонический `packaging/install_program.ps1` с этими путями.
Он завершился до создания отката и замены managed-файлов:
`Protected user path in manifest: eid3d_materials/maps/djwblade.json`.
Причина установлена в `Read-ProgramManifest`: защита запрещает компонент
`maps` на любой глубине. Материалы renderer содержат такой каталог.
Проверенная копия `tools/program_files.ps1` в staging содержит тот же запрет;
обход защиты, исправление скриптов или изменение staging не выполнялись.
Необходимый следующий шаг — отдельное исправление/согласование совместимого
канонического установщика; затем повторить установку неизменных бинарников282.

После отказа: `EIMapperMcp.exe --version` → `EIMapperMcp 280`, exit0;
все192 managed SHA соответствуют прежнему manifest.15111 пользовательских
файлов сохранили SHA256 и LastWriteTimeUtc;9 защищённых файлов (правки владельца
и регистрация мостов) сохранили SHA. Маппер из установки и игра отсутствовали.
Все зафиксированные мосты PID26868/17412/7476/35644 живы с прежним временем
создания. Процессы не завершались. Новых отодвинутых файлов нет; прежний
`EIMapperMcp.exe.old-ataman-618d1126ac084359a1caa3b5c9422140` сохранён.
Откат280→282 не создан, поэтому его VerifyOnly неприменим. Старые откаты
не удалялись. Настройки, включая выбор вида, не менялись; GUI, игра и DLL
не запускались/не загружались. Финализатор не выполнялся. `subst` пуст.

Локальные снимки и журнал отказа: `work/install-282-20261106/`.
Это блокер установки, а не новая функциональная проверка renderer.
282 и входящая281 пока НЕ УСТАНОВЛЕНЫ; принятие владельцем не отменено.
Без push; чужие строки ROADMAP и остальные незакоммиченные правки сохранены.