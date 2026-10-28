# EI-NAVMESH-NOGRID-234 — 2026-10-28

УСТАНОВЛЕНА234. База main69352cf5c19421cf94ac369dae9e4055ec02472b/233.
Реализация maincd4daf6c54f02133d6b9a1e4a5a64c7eb6898707.
По скриншоту владельца убрана голубая квадратная сетка границ тайлов Navmesh:
удалён только её draw из CView и swatch легенды. Данные и построение Nav64,
заливка, контуры полигонов, узкие места, острова и автоперепекание233 сохранены.
Существующее ожидание легенды в nav64_legend230_check приведено к новому виду.

Достаточная адресная проверка — сравнение настоящего production MainWindow/CView
233 и234 на одной приватной копии zone1. Окна WA_DontShowOnScreen, без активации;
снимки только собственного QGLWidget framebuffer, без экрана владельца.
В обоих режимах «Заливка»/«Контуры» голубые квадраты исчезли. В области карты
справа от легенды и ниже неба число голубых пикселей49900→0; видимые705 тайлов,
58579 полигонов human/algorithm24 сохранены. Число отрисованных вершин уменьшилось
ровно на8056 вершин квадратных границ:598224→590168 и393918→385862.
Легенда: storage-grid1→0. Оба запуска exit0; изображения просмотрены.
Снимки/квитанции: build-navmesh-234/{before,after}-{fill,contours}.png,
{before,after}.stdout,visual-comparison.json. Игровые карты не менялись;
запекатель не запускался, поскольку изменение относится только к отображению.

Свежий Release build-navmesh-234/source/build-release. Raw/staged CLI234/exit0,
SHA4 бинарников совпали. Чистый build-navmesh-234/build-staging-234-nogrid:
финализатор против принятой233 —176→176/0 удалений, validator PASS.
709 production-входов совпали со scoped-коммитом (кроме генерируемого qm,
с нормализацией окончаний строк): source-inputs.json.

Установлена в artifacts/EI-Mapper-Ataman-2-first-test после проверки закрытости
редактора и игрового хоста. Installer:176 verified,8 changed,0 added/removed.
Установленный CLI234/exit0. Все18751 пользовательский файл сохранили набор,
SHA/размер/native-mtime; все12 MCP — PID/creation time. Копия233→234 в
artifacts/rollback233-to234-nav-nogrid-20261028, VerifyOnly PASS. Только свой прямой
предыдущий откат232→233 удалён после проверки состава, хэшей, отсутствия reparse
points и последовательности manifest; чужие каталоги сохранены. Финализатор на
постоянной установке не запускался. Квитанции в build-navmesh-234:
install.log,user-preservation.json,bridge-preservation.json,rollback-verify.log,
rollback-generation-cleanup.json. Задача выполнена, без GitHub push.
