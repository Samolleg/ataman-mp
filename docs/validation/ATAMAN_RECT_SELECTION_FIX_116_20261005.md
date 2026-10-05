# Ataman 116 — точное выделение рамкой и кликом

Дата: 2026-10-05. Task: EI-RECT-SELECTION-FIX-116. Ветка main; исходная база 92d9e00cf128cfcb3bd36fe4cd72450c0b08fa45. Продолжение подтверждённого аудита EI-RECT-SELECTION-AUDIT-114-20261005 по прямому поручению «исправляй». Исправление, локальные проверки, scoped-коммит и стандартная тестовая передача разрешены. Игра, MOB/MPR/ресурсы пользователя и чужие изменения не меняются.

## Причина и исправление

Аудит114 показал 0 видимых пикселей и 544 picking-пикселя в прозрачном поле синтетического полигона на действующих shaders. Selection fragment не проверял альфу, selection VAO не передавал UV; очередь не сохраняла текстуру. При release рамка имела отрицательную высоту, reader терял две крайние строки, точечный reader был смещён на одну строку.

- select.fsh теперь отбрасывает texel alpha <=0.1 ровно как видимый fshader.fsh. CPart передаёт UV и масштаб в immediate и instanced selection, с VAO и legacy attribute-binding.
- CObjectBase использует ту же приоритетную цепочку текстур части, что и visible draw: partTextures → secondary для textureNumber2 → primary. Immediate selection и очередь получают эту текстуру. Неподготовленная текстура не выбирается, как и невидимая модель.
- RenderQueue сохраняет texture, привязывает её при смене пакета; существующий shareInstanceBatch разделяет разные texture/UV. Убран прежний bind при каждом submit объекта.
- Общий vertex shader применяет UV-scale также в instanced draw. Раньше visible instancing подавлял этот масштаб при u_partAnimated=false. Теперь visible и picking используют одинаковые UV при обоих способах отрисовки; terrain/uninstanced helper с u_partAnimated=false сохраняет масштаб1.
- Drag draw/release используют одну нормализованную QRect. selectionReadRegion ограничивает её viewport и переводит нижнюю включённую строку по H-1-bottom. Точка, строка и колонка сохраняют ширину/высоту1; пустые и полностью внешние рамки не читаются. Прямой точечный pick также использует этот reader.
- Один glReadPixels с проверкой размера qint64; pack alignment восстанавливается. Hash-set исключает повторный линейный поиск ID для каждого пикселя. Это изменение алгоритма, не измерение пользовательской задержки.
- Нормализация выбора целой assembly при попадании на одну часть сохраняется. Геометрический выбор вне рамки по bounds не добавлялся; невидимые/удалённые объекты остаются исключены.

## Доказательства

Свежий shadow build build-release-116-rect-selection; Qt5.12.12/MinGW7.3, release wrapper и оба runtime компонента PASS. --version: EI Mapper Endurance Edition116.

1. tests/selection_render_tests.pro + cpp: **42 assertions PASS, 0 FAIL**, native скрытый QOffscreenSurface/OpenGL NVIDIA RTX4070SUPER. Компилируются действующие shaders, actual source CPart и RenderQueue. Проверяются 4 направления, inclusive edges, точка/строка/колонка, clipping/пустой/full viewport; transparent margin0 ID и positive center; pixel-for-pixel visible/selection footprint при UV1 и0.5; реальный GPU instance buffer, два разных picking ID, разные текстуры/UV; morphed immediate queue fallback; alpha25/255 discard и26/255 retained; прозрачное поле пропускает picking к видимой геометрии позади. Explicit drawSelectInstanced возвращает true, поэтому тест не подменяет instancing обычной отрисовкой. Visible drawInstanced тоже совпадает с visible immediate. Итоговое selection.png просмотрено: только непрозрачный центр без полной площади полигона.
2. render_queue_tests: **15 PASS, 0 FAIL, 0 SKIP**.
3. --database-creation-check, --terrain-mob-height-check (19 assertions), --assembly-catalog-check PASS. Реальные команды/history/save/reopen/групповые сборки и предыдущая функция115 сохраняются.
4. --ui-widget-check остаётся exit1: те же **10 clipboard failures** как115, итоговый flags diff пуст. Один промежуточный прогон дополнительно отказал в modelGalleryWorks на синтетическом resource-generation/cache deadline. Отдельный --model-gallery-check PASS и повторный полный UI возвращает modelGalleryWorks1. Промежуточный отказ сохранён в ui-widget-flaky.out/err, не скрыт и не считается PASS.
5. Дополнительный --scenario-layer-check: **48 PASS, 2 FAIL** (synthetic recovery snapshot / unexpected modal warnings). Независимый запуск исходной сборки115 в отдельном working directory даёт ровно те же два отказа. Это сохраняющиеся baseline limitations, не полный PASS сценария.
6. Первые неудачи нового test harness исправлены: qmake-quoted source path заменён EI_TEST_SOURCE_DIR; morph fixture получил source vertex indices. Итоговые42 assertions выполнены после этих исправлений.

Инженерные результаты: build-tests-116-rect-selection/selection_render_tests/run.out, render_queue_tests/result.txt, *-native/check.out/err, *-baseline115/check.out/err, ui-regression-comparison.json, build-final.log. Синтетические test fixtures принадлежат репозиторию; игровые данные не копируются. Отдельный OpenGL ES runtime этим тестом не проверен. Native тест не является ручной приёмкой исходной карты пользователя.

## Передача

Fresh build-staging-116-rect-selection создана из127 verified managed files предыдущей установки; обновлены EXE/runtime/VERSION/Readme/current changelog. Staged --version116 до finalizer, raw/staged EXE hashes идентичны; finalizer и независимый validate_distribution PASS,127 payloads+manifest. EXE SHA256: 4A3B59E1F1793591C1CF24FD6267584F48EB6BD2027B69BF1BE2B6A48325A2B4. После финализации программа из staging не запускалась. ZIP отдельно не запрошен.

Постоянная установка пока115: редактор запущен (PID3300), замена отложена до закрытия по docs/TEST_BUILD_HANDOFF.md §3.2. Пользователю отправлено требование закрытия. Следующий шаг: после подтверждения и повторной проверки процессов обновить только managed files с проверенным rollback и hash-инвентарём всех unlisted user files; затем ручной тест на карте. Полная runtime-приёмка исходного симптома открыта.

Ручная проверка: протянуть рамку рядом с листвой в каждом направлении, затем пересечь видимые пиксели; повторить с Shift и одиночным кликом. Assembly по прежнему выбирается целиком. Повторить на использовавшихся деревьях карты; проверить hidden/части/снятие выделения. Исходные связанные задачи112–115 и их ручная приёмка сохраняются.
