# EI-WINDOW-MAXIMIZED-296 — диагностика 2026-11-08

**INCOMPLETE / NOT REPRODUCED / NO FIX.** Задача296 не закрыта. Причина
потери maximized у владельца не установлена. Код, VERSION295, CHANGELOG и
Readme не изменены; staging296 не создан. Не назначать этому аудиту статус
исправления или готовой версии296.

База main033d5d30507bb4d7ba878c738b6fba0c7526b2c7.295 запускалась только
из собственной копии build-staging-295-rebuild64-graphics-20261108.
293R2 e333369246100615c5ef881db357215195372d08 собрана из git archive,
свежим build_release.ps1, Qt5.12.12/MinGW7.3, без subst/junction.
Установленные и исторические EXE не запускались. Сборка293: exit0;
журнал work/window-maximized-296-20261108/logs/build293.log.

## Вход и способы наблюдения

Отдельный QGenericPlugin в work: настройки EI_MAPPER_SETTINGS_DIR/INI,
перенаправленные QStandardPaths::writableLocation, TEMP/APPDATA/LOCALAPPDATA,
EI_MAPPER_BACKUP_ROOT. Агентский доступ выключен. Начальный QByteArray Qt v3
совпадает с прочитанным у владельца: maximized2, fullscreen0, frame
(-1,-12)..(3840,2076), normal(2,65)..(3837,1284), screen0/width3840,
последний geometry QRect(0,40)..(3839,2075). Включительные координаты QRect
дают normal3836×1220; отдельный normal-вход задавал3835×1220 в geometry v3.

Читались UI-настройки и options владельца. Текущий ui/renderer был OpenGL;
для целевого прогона явно задан Rebuild64, иначе startupPulse условно пропускается.
Учтён uiScale105%. Пути ресурсов перенаправлены на294 замороженных файла;
auto-open выключен, игровые карты не открывались. Config64 не копировался.

Проверялись Windows+WA_DontShowOnScreen и offscreen. В первом режиме
флаг можно читать, но настоящее нативное разворачивание подавлено самим WA.
Во втором добавлен процессный QPlatformScreen3840×2076, обычный offscreen
не создаёт окон рабочего стола. EnumWindows по своему PID: видимых окон0
в записанных результатах. Computer-use/захват экрана не использовались.

## Наблюдение — это не приёмка296

| Вход | 295 | Собранная293R2 |
|---|---|---|
| maximized, данные/UI владельца, Rebuild64, offscreen | QWidget2 / QWindow2 при Show; после600мс QWidget2; exit0 | те же2/2/2; exit0 |
| normal, тот же профиль, offscreen | QWidget0; exit0 | QWidget0; exit0 |
| первый запуск без сохранённой геометрии/доков, offscreen | QWidget0; итог(2,2,4413,686); exit0 | тот же итог; exit0 |

Требуемое различие «295 после Show=0,293=2» не получено. Полная геометрическая
проверка также НЕ PASS: минимальные размеры панелей в offscreen расширяют окно
до5934/5970пикселей даже на экране3840; normal не сохраняет ширину3835.
Это ограничивает пригодность оснастки для исходного геометрического дефекта.
Сходная WA-проверка сохраняет2, но не проверяет нативное разворачивание.

Найден краткий2→0→2 в обработке изменения минимального размера;
в293 он тоже есть и окончательной потери флага не доказывает.
Дополнительный fullscreen-вход295 дал4→0, QWindow оставался4. Первое наблюдение0
в QWidget — при ChildPolished, в стеке QLayout::activate → doResize →
layout dock-виджетов → ensurePolished. Потеря наблюдается к Show. Точная
инструкция записи и связь с дефектом maximized НЕ установлены;
исправление fullscreen отдельно не заявляется.

Qt heartbeat10мс в Windows/WA-прогоне295 с данными/UI/Rebuild64:
max692.774мс; offscreen813.496мс,2931129.423мс. Эти числа характеризуют
диагностическую оснастку, а не сравнение295/готовой296. Ни одного исправления
не собиралось; критерий «296 без роста паузы» ещё не проверен.

Подробности: work/window-maximized-296-20261108/results.json;
cases/295-max-real-native,295-max-real-offscreen,293-max-real-offscreen,
295-normal-baseline,293-normal-baseline,295-first-baseline,293-first-baseline,
295-full-baseline: trace.jsonl,process.json,logs/workflow.log.
logs/fullscreen-stack.json содержит ближайшие PE exports; большие смещения
не являются достоверной символизацией закрытых функций Qt.
Ранние варианты оснастки не засчитаны: первая попытка offscreen без QPA DLL
завершена по таймауту собственным runner; более поздние используют настоящий
qoffscreen.dll. Искусственная задержка WINDOW_PROBE_DELAY — только отдельный
диагностический опыт, не замер запуска. Не приписывать ему PASS.

## Передача и следующий шаг

Нужен способ невидимого нативного запуска, который сохраняет реальную цепочку
ShowWindow/WM_SIZE. Offscreen использует другой QPA, WA подавляет native show.
Следующая адресная проверка: согласовать иной способ полной невидимости
нативного окна, затем трассировать QWidget/QWindow state на входе/выходе
двух post-restore startupPulse и первого native show, с точкой записи2→0.
До подтверждения этой цепочки гипотеза про startupPulse остаётся гипотезой.

Исходные артефакты295, SHA256:

- EIMapperEndurance.exe: de64617ca37c2c7a852c38cae79d230e1aee0beb59a8353711efa5618b273405
- ataman_rebuild64.dll: 0714441100e4a63e5a10f92e9d2a03511fe5d096e2bd73ff5b43336be6206bfe

Это хеши контрольной295, не несуществующей296. Сборка293 и её исходный экспорт,
все runtime-копии, приватные ресурсы и копии настроек удалены. Сохранены журналы,
оснастка и receipts. cleanup-receipt.json:294 frozen SHA совпали, различий0,
8565 файлов удалено, remainingData/293Source/293Build/runtimeCopies=false.
Чужие отслеживаемые файлы сверены по SHA с началом работы; ROADMAP сохраняет
исходный текст владельца побайтно после собственного блока296.
Установка, реестр владельца, live-мод и Config64 не изменялись;
процессы владельца/мосты не завершались, push не выполнялся.

Справка о поведении Qt: [QWidget Qt5.12.12](https://raw.githubusercontent.com/qt/qtbase/v5.12.12/src/widgets/kernel/qwidget.cpp),
setGeometry_sys очищает Maximized/FullScreen при фактическом изменении размера;
это возможный механизм, не доказанная причина дефекта владельца.
