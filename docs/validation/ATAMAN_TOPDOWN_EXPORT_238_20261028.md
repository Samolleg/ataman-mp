# MCP-TOPDOWN-EXPORT-01 — Ataman238

Поручение28.10.2026: реализовать приложенное MCP-TOPDOWN-EXPORT-TASK.txt.
База main `eeef1ea33c9050b3ad79c9bc1f9651d450acb136` /237. Работа в main,
без push, приватных исходников движка и изменений живых игровых данных.
Реализация: `6867d6e4b299129844e0dea12d4ce4d83480986a`. После неё только
квитанция установки;695 production-входов сопоставлены с Git-blob исходников.

## Реализация

`export_map_topdown` доступен в динамическом каталоге/ataman://tools и через
штатный stdio bridge. Сразу возвращает ID и deadlineMs120000; ресурсы,
отдельные GL-проходы, detached PNG writer и публикация проходят асинхронно.
Read-only/Save-off допустимы; annotations: readOnlyHint=false,
destructiveHint=false. Старые дедлайны остальных методов сохранены.

Используются CView::draw, реальные секторные VBO/атлас, CPart/RenderQueue,
обычный FIG/BON/MMP loader. Отдельного рендера/парсера MPR нет. Mesh-части
клонируются без создания CNode; текстуры удерживаются. Проверяются полнота
геометрии, winning providers, загруженная identity и изменение снимка.
Пустой родительский узел MOD допустим, если его дочерняя геометрия полна.

Восемь настоящих проходов, preview и manifest; каноническое +X вправо/+Y
вверх, без gameImage conversion. Вода2/3 depth-тестируется против рельефа
без сооружений; текстурная прозрачность/волны не загрязняют маску. Наземные
overlay остаются в terrain; травяной материал4 — в vegetation. Авторские
trees/bushes, нативные flora и проверенная иерархия legacy samt56z1 исключены
из composite/structures. Магические ловушки — логические effect-маркеры,
как свет/звук/частицы, а не здания. Остальная статическая геометрия учтена.
Семантическая roads-mask недоступна; дорожная фактура остаётся в renderer.

Output проверяется относительно workspace и источников профиля/игры.
Junctions/reparse points запрещены. Win32 leases с LIST_DIRECTORY удерживают
маршрут; READ_ATTRIBUTES без этого доступа не предотвращает rename.
PNG/JSON пишутся в новый incomplete-каталог; только полный комплект получает
complete manifest и атомарное имя назначения. Отмена/ошибка не чистят чужие
файлы; готовые экспорты не зависят от TTL операции.

## Проверка production MCP и геометрии

Исходники обвязки: tests/topdown_export_check.cpp/.pro и
tests/run_topdown_export_check.ps1. Игра/интерактивный GUI не запускались.
Bridge сам запускает настоящий Release EXE с --agent-headless; тестовый EXE
не используется как экспортёр основного background-сценария. Отдельный
MainWindow вне экрана проверяет несохранённые/скрытые состояния через MCP.
Карты и полный профиль ресурсов — приватные копии в build-topdown-238.

- **Финальный native run14:284 PASS/0 FAIL, exit0,107,149с.** Это проверки
  разных свойств, а не284 независимых пользовательских сценария. Предыдущий
  run11:279 PASS/0 FAIL,98,384с; все89 защищённых внешних входов совпали.
- Zone1:352×288 world, **1408×1152**,1982 объекта:590 structures,
  1196 vegetation,196 excluded;3903 mesh-части,298 resource identities.
- Zone2:160×192 world, **640×768**,828 объектов:65 structures,
  717 vegetation,46 excluded;1478 mesh-частей,121 resource identity.
- Все9 PNG каждого комплекта проверены по размерам/байтам/SHA-256; обе
  маски бинарные и содержат реальную геометрию. Manifest объясняет каждый
  MOB object record и отдельно mesh-части. Source-file SHA, baseline,
  dirty, revision, snapshot и resource identity разделены.
- Независимое пересечение треугольников с **фактическими GPU index buffers**:
  5580 wet и4201 dry-over-overlay sample, **0 расхождений** с water-mask.
  Четыре разнесённые mesh-точки мостов/сооружений stbr6/1982,
  stbr6/1302,gpjunst01/1456,gpjunst03/1482 совпали по матрице с допуском1px.
  Записаны береговые точки на реальной границе маски;632 пикселя мостов
  перекрывают непрерывную воду под ними.
- География background/editor совпала побайтно по structures-mask при
  скрытом доме и выключенных viewport-слоях. Unsaved move остаётся dirty;
  дополнительная MOB включена; markDeleted исключён; повёрнутая крыша с
  origin за bounds обрезается по геометрии и остаётся видимой.
- NO_MAP, stale/mid-operation REVISION_CONFLICT, реально отсутствующая
  модель и текстура, junction/existing/forbidden output, GL size refusal,
  cancel resolving/capturing/writing, отсутствие complete partial manifest,
  отказ shader renderer без закрытия viewport и конфликт авторского освещения
  даже без нового Undo index
  и освобождение слота проверены. FBO/viewport/projection/camera/MOB/hidden/
  blend/depth/color masks и документы сохранены. Старый minimap bake работает.
  Qt FBO отдаёт ARGB32_Premultiplied (format6), прозрачный readback сохраняется.

Данные проверки: build-topdown-238/run14/stdout.txt,process.json,
geographic-controls.json,depth-diagnostic.json,mcp-catalog.json.
Полные экспорты: build-topdown-238/run14/exports/zone1 и zone2.
Каждый содержит manifest.json и preview.png. PNG/игровые данные не в Git.

При усилении проверки выяснилось, что старый CPU picking CTile использует
другую диагональ метровой ячейки, чем renderer. Для oracle прочитаны реальные
GPU-индексы; общий picking не менялся и остаётся отдельной находкой.
Промежуточные отказы зафиксированы в run1–run10; они не объявлены PASS.

## Сборка, сохранность и передача

Штатный Release238: Qt5.12.12/MinGW7.3x64, оба runtime-компонента собраны
wrapper заново. Raw/staged --version238/exit0 и4 SHA совпали. Проверенный
mapper SHA256 `2D4CD3AC2B203ECF0E7B17838AAEE0BDD86350039F00B18B45998A1703E2D98B`,
bridge `713FAE8D955C0DF00A535A479997238314F454200E88BA5960E5C5F1A5C4587F`.
Staging: build-topdown-238/build-staging-238-topdown-final; finalizer и встроенный
distribution validator PASS:186→186 managed,0 removed. Квитанции:
build-topdown-238/staged-identity-final.json,staging-final.log,finalize-final.log.

Общий guard run12 **не PASS**: во время проверки владелец/внешний процесс
изменил installed endurance-options.json (SHA/размер/mtime); он сохранён,
не восстанавливался. Остальные88 входов совпали. **Run14:89/89 guard PASS.** Ранее run8 обнаружил внешнее
изменение texts.res; оно также не откатывалось. Run11 отдельно подтвердил
неизменность всех89 своих входов. Новые чужие данные не заменяются старыми.

**INSTALLED238.** После сообщения владельца о закрытии редакторов свежий
preflight чист. Штатный install_program:186 verified,8 changed,0 added,
0 removed. Постоянная папка artifacts/EI-Mapper-Ataman-2-first-test;
installed --version238/exit0. **19281 пользовательский файл сохранил набор,
SHA/размер/native-mtime;13/13 мостов сохранили PID/creation time.** Retired
journal:0 удалено,3 загруженных образа сохранены. Новая резервная копия
artifacts/rollback237-to238-topdown-20261028 проверена штатным VerifyOnly:
ROLLBACK_VERIFIED. Прежние чужие rollback не изменялись. Квитанции:
build-topdown-238/install.log,install-user-before.json,install-user-after.json,
install-bridges-before.json,install-bridges-after.json,rollback-verify.log.
Финализатор на постоянной папке не запускался. Старый финализированный
промежуточный staging не изменялся; установлен именно topdown-final.

Для загрузки обновлённого bridge EXE нужен один штатный MCP reconnect клиента;
работающие мосты намеренно не завершались. Каталог server-side динамический;
новый метод появляется в ataman://tools после подключения к238.

Физическая проверка видимого окна/другого GPU остаётся владельцу; она запрещена
разработчику вместо пользователя. Реальный120-секундный timeout не форсировался;
deadline явный и ограничивает весь lifecycle, общий отказ/отмена с восстановлением
проверены. Художественная стилизация, регистрация миникарты и MMP не входят.
