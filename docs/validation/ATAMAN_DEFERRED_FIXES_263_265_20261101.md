# Ataman263–265: outliner, texts.res и стабильный экспорт

Задача EI-DEFERRED-FIX-263-20261101. Поручение владельца01.11.2026:
исправить все три отложенные проблемы предыдущего аудита. База main
`a6a3e0ad0a36ed31ec7934082ff7f91bf0a97fdd`, программа262. Изменения выполнялись
в main; чужие ROADMAP/QM сохраняются. Игровые данные не изменяются,
видимый редактор/игра не запускаются, push не выполняется.

## Outliner263

Причина: загрузчик активирует основной и зависимые MOB по очереди и возвращается
к основному. Каждая активация вызывала полное обновление дерева/свойств.
`finishRefresh` также повторял фильтрацию при восстановлении непустого выбора.

Во время workspace load запросы объединяются. В конце дерево и свойства
обновляются один раз. Сохраняются keyed rows, раскрытие, фильтр, выбор и скролл.
На время обработки событий загрузчиком дерево отключает действия и очищает
raw bindings старого MOB/logic; QObject-ссылки объектов и logic focus используют
QPointer. После удаления MOB устаревшие строки удаляются итоговым refresh.
Обычное редактирование вне workspace load сохраняет немедленное обновление.

Проверка на финальном EXE:41 PASS в `--object-hierarchy-check`. Покрыты
Ctrl/Shift/клавиатура, группы, переименование, создание, Delete/Undo/Redo,
две MOB с одинаковым ID, behavior/logic, русские alias, фильтр и5000 объектов;
добавлены пакетная загрузка, безопасные callbacks при удалении MOB,
идемпотентное завершение и инвалидирование уничтоженного logic owner.

| Один процесс,5000 объектов | Три immediate refresh | Три объединённых запроса |
|---|---:|---:|
| Фактические проходы дерева/фильтра |3|1|
| Финальный контрольный замер |175мс|77мс|

Первый прогон дал247/108мс; второй был нужен после добавления проверки lifetime.
Это локальные замеры одного сценария, не оконный FPS. В настоящей загрузке zone9
промежуточные активации MOB показывают outlinerMs:0.
Evidence: `work/deferred-fixes-263-20261101/object-hierarchy-check/`.

## texts.res264

Причина: диалог читал/удерживал весь архив, перед Save As повторно читал весь
destination, а полная пересборка, проверки, backup и запись выполнялись в GUI.

Открытие теперь читает индекс и только map.txt. В GUI остаются проверка редактора
и сериализация небольшого map.txt. Файловая транзакция выполняется QtConcurrent
worker на копиях значений, без доступа к QWidget/живому документу. Диалог
блокирует controls/open/reentrant Save/закрытие; общий write guard удерживается
с момента dispatch до обработки результата. При принудительном уничтожении
владельца destructor дожидается worker. Результат и отчёт публикуются в GUI.

Потоковая одночленная замена использует блоки4МиБ. В отличие от общей операции
добавления/удаления RES members, она сохраняет исходный порядок/slots directory,
имена, metadata, padding и остальные байты. Меняются map.txt и необходимые
размеры/смещения. Fresh staged hash/reopen, source/destination guards,
проверенная backup и атомарный QSaveFile с отключённым direct fallback сохранены.
Изменения других членов до начала сохранения включаются в новый результат;
изменение map.txt отклоняется. Гонки во время записи также отклоняются.

-31 QtTest PASS, включая grow/shrink/empty/no-op, точное совпадение с прежним
  byte-preserving serializer, Save As/new/overwrite, backup, merge чужого члена,
  stale map.txt, source/destination race, появление destination и имитацию отказа
  перед atomic commit. Старые проверки RES/travel/publication также прошли.
-27 native PASS с настоящими скрытыми QWidget.32-МиБ синтетический архив:
  open3мс, dispatch с созданием progress105мс, весь save/readback test524мс,
  74 GUI heartbeat события, максимальный промежуток115мс включает dispatch.
  Цифры не означают, что GUI вообще не выполняет синхронную работу.
-Настоящий приватный texts.res DejaVu1465602байта открыт в диалоге, сохранён
  через Save As и заново открыт. SHA исходного и результата совпали целиком.
  Это копия, не архив установленной игры.

Evidence: `work/deferred-fixes-263-20261101/qt-res_archive_document_tests/`,
`final-texts_async_check-r3-run/` в том же каталоге. Диагностический r1 аварийно
завершился из-за несовместимых layout test-only request fields при линковании
QtTest TU с production objects. Из focused native target удалён QT_TESTLIB_LIB;
r2/r3 используют production ABI и проходят. Это был отдельный тестовый EXE,
не установленная программа. QtTest suite собирает все свои TU с одинаковым ABI.
Предупреждения QTemporaryDir/locking при cleanup асинхронных backup и отсутствие
шрифтов offscreen Qt не выдаются за отдельные PASS; функциональных отказов нет.

При заключительной сверке обнаружено внешнее изменение live texts.res относительно
старого fixture. Поэтому сделана новая приватная копия актуальных1465575байт и
повторён UI Save As/reopen: r4 также27 PASS, результат совпадает с исходником
побайтно. Хеш live-оригинала до копирования и после проверки совпал; этот файл
не записывался. Evidence: `current-texts-guard.json`,
`current-texts-verification.json`, `final-texts_async_check-r4-run/`.

## Цвет экспорта265

Подтверждённая причина: opaque queue сортировалась по адресам GPU texture/mesh.
При GL_LESS совпадающие по глубине поверхности выбирают первый fragment.
Перемещение ресурсов между адресами меняло цвет некоторых пикселей.

Экспорт использует stable content keys текстур и геометрии, без путей и адресов.
Геометрия сериализуется из восьми float-компонент без padding и хешируется один
раз на уникальный mesh. Обычная очередь рендера сохраняет прежний порядок.
Transparent depth/sequence и проверка совместимости instancing не заменяются
content keys: реальные разные GPU meshes не объединяются по одному хешу.

-16 QtTest PASS render queue, включая перестановку адресов и прозрачность.
-15 native GPU PASS. Две coplanar поверхности меняют цвет при старом порядке;
  при stable order все framebuffer pixels совпадают после перестановки ресурсов.
  Регресс instancing/picking также прошёл:24 ID,2/4 draw calls по состоянию теней.
-Два полных zone9 в независимых процессах с разным порядком прогрева:
  51+53 PASS.2125 объектов,10239 mesh parts,479 ресурсов приватного fixture.
  Все11 PNG побайтно одинаковы: структуры, composite/previews, terrain, vegetation,
  relief, water и masks. Оба прогона проверили неизменность контекста/revision
  и отказ при действительно отсутствующей текстуре, без публикации неполной карты.

Evidence: `work/deferred-fixes-263-20261101/export-png-equality.json`,
`final-instancing_pixels_check-run/`, `qt-render_queue_tests/`;
`work/zone9-export-255/deferred265-r1/` и `deferred265-r2/`.
Экспорты заняли16542/12998мс при пересекавшихся инженерных проверках. Сравнение
скорости с прежней262 не заявлено. Детерминизм подтверждён на текущем GPU/драйвере
и одинаковой сцене; междрайверное/межплатформенное совпадение не проверялось.

## Итог проверок и границы

47 QtTest +187 native assertions PASS. Сборка сделана в новом source export и
новом shadow-каталоге; оба runtime собраны из исходников. Позднее менялись только
диагностические ветки main/object_hierarchy_check: source timestamps обновлены,
соответствующие объекты пересобраны и затронутая проверка повторена.

Общий `--ui-widget-check` на Windows **недоступен**, exit2 до synthetic writes:
`Native clipboard format 2 has no preservable synthesis source`.
Буфер обмена с изображением не очищался и не подменялся. Связанные native
UI-сценарии outliner и texts.res проверены отдельными clipboard-free targets.
Недоступный общий тест не считается PASS. Ручная визуальная/игровая приёмка и
оконный FPS остаются за владельцем; редактор интерактивно не запускался.

## Передача

**Установлена265** в `artifacts/EI-Mapper-Ataman-2-first-test`.

| Версия | Scoped-коммит main |
|---|---|
|263|`859c0ad991fce23d8ea2b0b4ded15cd26e50915e`|
|264|`9bcf4858b9b2fcbf5d6d8b4487f6f7b28275dea8`|
|265 / исходники сборки|`7668c4a3d8a87b23becdcca9ec3097e17af7403a`|

-532 tracked source files сверены с исходниками сборки; допустимы только
  CRLF/LF различия. Raw/staged показывают265; четыре program/runtime SHA равны.
-Clean staging/finalizer PASS:186→186 managed paths,0removed.
-Guarded install:186 файлов проверено,8заменено,0добавлено/удалено из manifest.
  Сохранены метаданные13942 unmanaged файлов и SHA двух корневых JSON.
  Удалён только один прежний retired binary по реестру и hash/busy guard.
-Installed manifest SHA256:
  `8722FAB3FD20D97D61D516EC6DA8502990B348B29F6950945F57CBD3A496DA0F`.
-Installed EXE SHA256:
  `68BDB9E1906E069C09289635AD5CE042A7554149532F8AEE7443BB6890865B85`.
-Verified rollback к262:
  `artifacts/rollback262-to265-deferred-fixes-20261101`.
  Прежний255 удалён только после проверки обоих backup, цепочки manifest,
  точного абсолютного пути, отсутствия reparse points и всех190файлов.
-90 приватных входов не изменились. Девять live-входов отличаются от старого
  снимка, включая обновлённый texts.res; это не объявляется изменением этой задачи.
  Актуальный texts.res дополнительно проверен отдельно, как описано выше.
  Чужие QM и исходное содержимое ROADMAP сохранены.

Evidence: `work/deferred-fixes-263-20261101/compiled-input-verification.json`,
`package.json`, `installation.json`, `rollback-retirement.json`,
`input-verification.json`. Критерии трёх исправлений выполнены; ограничение
общего clipboard-зависимого UI-теста и ручная приёмка явно оставлены выше.
