# MCP-TOPDOWN-EXPORT-01-ADD — Ataman239, 29.10.2026

База main `346bd8d55947886571eb6d03a8e1d6dbf819ee2b` /238. Дополнение
владельца к исходному заданию: snapshot + читаемая основа + MCP-просмотр
зарегистрированных слоёв/фрагментов. Состояние на подготовку: VERIFIED / PREPARED239,
без push. Рабочие данные, настройки и чужие dirty-правки сохраняются.

## Реализация

- `window_snapshot({})` внешне возвращает text metadata + стандартный image/png
  content. profile/map/capturedRevision привязаны к моменту захвата. Локальный
  сервер немедленно возвращает operation ID, preparing/capturing/encoding,
  deadline30000. Bridge ожидает статус, передаёт notifications/cancelled в
  cancel_operation, продолжает принимать другие запросы между опросами.
- Текущая камера рисуется штатным draw в собственный FBO owning GL-thread.
  Не вызываются camera.update(), QWidget::grab, swap, показ/активация окна.
  Сохраняются projection/camera/framebuffer/viewport/маски записи/GL state.
  PNG кодируется worker-thread; один PNG не превышает384 КиБ, с запасом
  для512-КиБ IPC envelope. Размер ограничен1024×768 без растяжения.
- Незавершённый open_map/close_map/create_map известен bridge по полученному ID:
  snapshot немедленно отвечает BUSY/operation до терминального operation_status.
  Это не ставит ещё один короткий запрос в занятую очередь GUI-коммита.
  Сервер также проверяет commit/loading/BUSY. Общие сроки остальных методов
  не увеличены; RENDER_UNAVAILABLE/SNAPSHOT_TIMEOUT/отмена не закрывают исправный IPC.
- `cartographic-base.png`: нейтральный рельеф из точного relief, однотонная вода
  из water-mask, сооружения/контуры внутри structures-mask. Нет растительности,
  смещения, выдуманной географии и генеративных деталей. Основное preview
  показывает эту основу; composite-preview сохраняет цветную сверку.
- В renderer нет однозначного авторского road-mask. Дорожные материалы сохранены
  в terrain/composite. `cartographicBase.status=limited` и warnings прямо указывают,
  что дороги следует смотреть в текстурном проходе. Complete означает целостность
  файлов, не отсутствие ограничений читаемой основы.
- `read_export_image(exportId,layer,pixelRect?,maxSide=1024)` возвращает image content,
  исходный pixelRect/sourceSize/worldBounds. Только PNG зарегистрированного manifest,
  проверяются SHA/размеры/маршрут/границы. Регистрация хранится в защищённом
  automation/exports отдельно от операций и привязана к установке/workspace.
  Нет произвольного чтения по пути. Комплект/просмотр не удаляются по TTL;
  manifest.retention: до удаления владельцем комплекта или регистрации.

## Диагностика исходного snapshot

Подтверждённый код238: bridge local() ограничивал весь синхронный вызов2сек и
на превышении уничтожил бы соединение; сервер выполнял QWidget::grab, явный
paintGL, grabFrameBuffer, композицию, масштабирование и PNG до ответа.
paintGL также имеет guard для невидимого/минимизированного виджета. Этот guard
сам по себе НЕ доказан причиной наблюдения владельца: WA_DontShowOnScreen и
isVisible — разные состояния.

Контроль неизменённых production-бинарников238 в собственной изолированной папке
успешен: настоящий bridge/background + Zone1,1475,9401мс, кадр содержит сцену.
`build-topdown-239/legacy-probe/result.json`, legacy-snapshot.png; SHA обоих EXE
совпали с установленной238. Нестабильный исходный timeout237 в этом запуске не
воспроизведён; точная задержавшаяся фаза того случая не установлена и не выдаётся
за установленную. Устранён подтверждённый механизм отказа: тяжёлый синхронный
снимок/очередь открытия больше не являются обычным2-секундным IPC-вызовом.

Измерение прежней последовательности в изолированном native editor run3:
windowGrab1196мс, явный paint49мс, framebuffer2мс, композиция/PNG25мс,
всего1272мс; viewport2047×312, окно2593×800. Это измерение последовательности,
не отдельный запуск неизменённого238. Новый snapshot run3: background1258мс
(capture1139), прогретый editor126мс (capture77), после изменения камеры114мс
(capture36). Краткие метрики сохранены в *.png.json / legacy-pipeline-timing.json.

## Достаточные проверки

Финальный `build-topdown-239/run3`: **334 PASS /0 FAIL, native exit0,
95,6880609с;89/89 внешних контрольных файлов без drift**.
run1:330/0; run2:331/0, также89/89. После run2 изменён только bridge для
немедленного BUSY при pending open; run3 проверяет эти финальные бинарники.

Настоящие stdio bridge и production `--agent-headless`; отдельный native
MainWindow/CView работает вне экрана, без пользовательского GUI/Computer Use.
Изолированы AppData/User/Temp/settings/automation/backup; реальные карты и
ресурсы читаются из приватной копии238. Тестовый EXE не заменяет экспортёр.

Проверены:

- snapshot Zone1 в background/editor, реальная непустая GL-сцена и image content,
  ограничения PNG/размера и metadata; изменение только камеры даёт другой кадр,
  документы/revision/камера сохраняются;
- настоящий open_map → немедленный snapshot BUSY с тем же ID, без timeout;
  отмена MCP-запроса, RENDER_UNAVAILABLE и get_context без reconnect;
- точное восстановление GL target/viewport/color/depth/blend после snapshot
  и сохранность камеры, shader refusal без закрытия документа;
- ветка SNAPSHOT_TIMEOUT/phase проверена старением created на31сек в native
  сервере; это проверка реальной ветки deadline, не30сек зависшего GPU;
-12 файлов Zone1:1408×1152; Zone2:640×768. Девять полноразмерных слоёв
  согласованы по охвату/размеру; два уменьшенных preview и manifest schema2;
- светлая основа следует маскам, нейтральна на суше, без растительности/цветной
  фактуры. Отдельный limited-статус дороги проверен;
- все11 PNG доступны как MCP image content, точный150×120 crop моста совпал
  с исходным structures PNG; просмотр после нового запуска bridge;
- фактический завершённый editor export оставлен без terminal read, finishedAt
  состарен на61сек, штатный prune освободил слот; read_export_image продолжил
  работать. Не требуется ожидание TTL в реальном времени;
- неизвестные пути/выход crop за bounds отклонены;
- регресс238: hidden/dirty/два MOB/deleted/граница, missing model/texture,
  revision/environment conflict, отмена всех3 фаз, lease/junction, GL limit,
  прежний minimap;9781 CPU/GPU depth samples без расхождений,632 bridge/water
  pixels,4 разнесённые mesh-точки и береговые точки.

Агент просмотрел полученные через MCP snapshot, preview, water-mask, relief,
structures, terrain,150×120 фрагмент моста и400×200 фрагменты ворот/дороги.
Доказательства в run3/view-*.png, bridge-*.png, gate-road-*.png,
background-snapshot.png, editor-snapshot.png, editor-camera-snapshot.png.
Полные комплекты: run3/exports/zone1 и zone2. Геометрия экспорта238 сохранена.

## Сборка и передача

Свежая Release `build-release-topdown-239`, штатный build_release.ps1,
Qt5.12.12/MinGW730_64; оба32-битных runtime-компонента пересобраны.
Изначальные ошибки окружения qmake/PSModulePath устранены настройкой только
окружения команды; код vendor19/lock не менялся. Нет auto-review rejection.

Raw/staged --version239/exit0 для mapper/bridge и4 бинарных SHA совпали.
Чистый staging `build-topdown-239/build-staging-239-topdown-add`:
финализатор186 prior→186 current,0 removed PASS. После финализации не запускался.
SHA mapper `8BA30A122201151F5F27D174DE0D26AF61A79C8259B88167A3969D977CEEE6B2`;
bridge `5EE1FEE5E8D4A22A53FB96DF902BE21E553CE8BD0D571D1FEB0B0422DF4B10A9`.
Квитанции: staging-identity.json, stage.log, finalize.log в build-topdown-239.

Осталось выполнить штатную установку и записать source SHA/сохранность файлов.
Видимое окно/другой GPU и игровая упаковка MMP не проверялись. Семантическая
roads-mask недоступна; исходный нестабильный237 timeout не воспроизведён на238.
