# Ataman184 — фризы копирования, вставки и перемещения

2026-10-15, EI-OBJECT-EDIT-FREEZE-184. База main589573ba946de25aabdb155364049ea89e835920/183. Поручение: исправить паузы копирования/вставки/перемещения, сохранить ID checks, качество миникарты и Undo/Redo, проверить и установить. Работа лично в main, чужие изменения и игровые оригиналы сохранены. Первичный аудит: ATAMAN_OBJECT_EDIT_FREEZE_AUDIT_20261015.md; его журнал относится к182, не является измерением183.

## Изменения

1. Обычный Ctrl+C использует уже существующий verified rawObjectTemplatesBytes: source+clean MOB разбираются и сопоставляются один раз на MOB всего выделения. Каждая запись по-прежнему проверяет исходный SHA/provenance, выдаёт точные raw bytes; при отказе batch копирование прекращается до изменения буфера. Разные MOB группируются отдельно. Ранее обычная ветка вызывала двухдокументный parse/alignment для каждого объекта.
2. Ctrl+V сохраняет каждый вызов штатного семейного allocator/резервирование ID и атомарную отмену частичного preview. После успешного создания списка дерево обновляется один раз. Allocator, validated candidate, проверка зависимых MOB и invalid-live guard не изменены.
3. Групповой transform хранит прежние CChangeProp/CChangeLogicParam/CAlignModelCommand дочерними в одной CObjectEditBatchCommand. Redo/Undo применяют их прежние действия/порядок, затем один раз пересчитывают dirty projection и панель. Дочерняя структура сохраняет AI invalidation. Одиночные действия и paste batch остаются прежними; ничто не объединяется между разными пользовательскими жестами.
4. getTerrainPos передаёт frustum существующему terrain renderer. Внекадровые секторы больше не отправляются на каждом mouseMove; GPU depth query, геометрия, порядок оставшихся секторов и соглашение unproject не меняются.
5. Автоматическая миникарта ждёт завершения pending gesture. Неиспользуемая генерация для выбранной embedded-карты лишь инвалидируется. Полный GPU захват остаётся в GL-потоке в исходном разрешении128px/sector; CPU bake/grade/MMP encode выполняются QtConcurrent worker по неизменным изображениям. Generation/context/pending/lock проверки не дают устаревшей задаче опубликоваться. Явный экспорт/Test Current Map получает текущую полную картинку синхронно и отзывает право публикации старого worker. Prepared grading применяется только при совпадении настроек/формата. Логи теперь отдельно показывают GUI capture и worker time.

## Одинаковый baseline183 и184

tests/object_edit_performance_check.{cpp,pro} связан с production objects проверенных Release183/184 и существующим native entrypoint. Скрытый настоящий MainWindow, полное действие Ctrl+C/Ctrl+V, клик фиксации preview, групповая история; собственный MOB2000 объектов/1502140 bytes и24 selected. Для baseline используются сохранённые из принятой183 заголовки, поэтому ABI не смешивается с новой184. Плоская карта24×24 sectors/3072×3072 review создана в temp из donor structure zone3obr.mpr под новым namespace; игровые данные/видео не входят в Git или пакет. Для качества добавлены8 собственных цветных фигур с alpha cutout. Рабочий каталог, настройки и object clipboard изолированы; системный clipboard performance-тест не меняет.

Финальное последовательное сравнение без конкурирующего запуска других наборов (один замер каждого действия, не распределение/p95):

| Полное действие |183, ms|184, ms|
|---|---:|---:|
|Копирование24|2728|113|
|Вставка preview24|4591|321|
|Клик фиксации вставки|32|30|
|Undo вставки|87|83|
|Redo вставки|419|394|
|Фиксация перемещения24|871|45|
|Undo перемещения24|871|45|
|Redo перемещения24|869|46|
|20 запросов земли|170|7|

Полный синхронный capture миникарты106→85ms (без MainWindow grading/encode). Новый automatic GUI capture26ms, остальная обработка worker; это другой разрез, не оценка общего времени прежней автоматической генерации. Heartbeat worker:6 pulses,max gap18ms; GUI не ждёт CPU image processing. Стоимость GPU capture сохраняется и зависит от карты/драйвера; асинхронность не является обещанием нулевого времени всей миникарты.

## Сохранность и регресс

- Native edit/async25 PASS/0 FAIL. Exact183/184: COPY JSON SHAec7947747de37704093f544a6a86f34fbd0b3bb9b72952005176a254781470bd; весь MOB после move+Undo+Redo SHA6faaf50ea21c291cea16c788607d614c8698d52b403a2cf6d6b07cb10e83ba84;20 GPU hit coordinates SHA9e4de05edbe38fd225d99742e8542d332f18b41c606ccfa8f46005347d2b9537. Все2024 IDs уникальны; один paste history action.
- Лит/неосвещённые pixels183=184: f44a3517a89038d076298a057eeb95838e16ef45726095dfa9c410554b11bfeb /2945de3c84d2c122eddb2793439846cd71f7d6ef11038dda0f8ded2713f6461d. Полный review и game-image split bake равны синхронному; asynchronous grading и encoded MMP побайтно равны прямому результату. Проверены invalidated generation, смена workspace, explicit generation при pending worker, embedded mode, lock и ожидание жеста.
- Штатные production suites: assembly-catalog91 PASS (включая семейные ID, live auxiliary IDs, exhausted range/atomic rejection, invalid live snapshot,143-object batch, Undo-held IDs, восстановление/повреждение зависимости, raw save/history/reopen); group-rotation26 PASS; model-connection42 PASS; pointer-modes56 PASS.
- Сохранность исправлений: native mixed properties18226/0 и rectangle selection18336/0. Всего302 адресных assertions PASS. Fresh Release184/runtime components/version PASS; git diff --check PASS.
- Общий --ui-widget-check выполнен, exit1: gallery open305/select299ms при100ms и selection7164 textures median396.124ms при200ms. Это известные категории отказов182; здесь не заявляется полный UI PASS или устранение всех задержек галереи/выбора. Функциональные flags миникарты/поля/Undo/clipboard прошли. Повторный общий UI не запускался ради тех же неизменённых условий.

Evidence: build-edit-184-20261015/{release.log,final-rebuild.log,baseline-native.out,candidate-native.out,comparison.json,checks.json,pointer-check.json,properties-candidate-native.out,rectangle-candidate-native.out}. Первый pointer-check exit2 был без EI_TEST_TERRAIN_FIXTURE; настроенный запуск прошёл. Промежуточный performance harness сначала не фиксировал обычную вставку Enter и использовал неразмеренный дочерний GL viewport; это исправлено в тесте до финальных измерений. Исходный unproject использует край пикселя: проверка допуска учитывает его субпиксельную ошибку, а exact hit bytes всё равно обязаны совпадать. Один heartbeat попал в первую тяжёлую отрисовку скрытого окна; финальный контроль выполнен после прогрева renderer, обычная обработка worker проверена отдельно. Эти промежуточные отказы не объявлялись PASS.

## Передача

Fresh staging/finalizer и guarded установка184 по TEST_BUILD_HANDOFF. На постоянной установке финализатор не запускается; данные/настройки сохраняются,182/183 исправления присутствуют. Ручная приёмка исходного набора объектов/драйвера/миникарты владельцем отдельно от synthetic native evidence. Алгоритмы переходов/рендера не заменялись, GitHub этим поручением не обновляется.
