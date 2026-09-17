# EI-BUILD-001 — первая бинарная сборка Ataman

2026-09-17. Версия 2, исходный SHA
`4ace67e70ac14608e0b6f17c00c00b73c7d356ff`, ветка `codex/rules-fix-v2`.
Документ и карта добавлены после сборки; исполняемые исходники не менялись.

- Release: `scripts/build_release.cmd`, Qt 5.12.12 win64_mingw73, MinGW 7.3,
  MSVC 2022 x86 для bridge/host. Инструменты Qt из Endurance использованы
  без изменения. Выход: `build-release-2-first/release`.
- Первый запуск прерван входящим сообщением; продолжен тем же wrapper в
  собственной новой shadow-папке. Итоговый exit 0; exe и оба runtime-компонента
  созданы. Логи: `build-first.log`, `build-first-resume.log` (не коммитятся).
- --version: `EI Mapper Endurance Edition 2`, exit 0.
- --database-creation-check и --resource-navigation-check: exit 0, PASS.
- --scenario-layer-check: в песочнице FAIL создания backup в AppData/Local/qttest,
  повтор в песочнице timeout 30 секунд; вне песочницы exit 0, 47 passed / 0 failed.
  Журнал прямо указывает Cannot create backup directory. Это ограничение среды
  первого запуска, не доказанный дефект сценарного кода.
- --ui-widget-check: offscreen timeout 120 секунд; остановлен только собственный
  процесс теста. Последний вывод russianLayout. Причина таймаута не установлена.
  Результат НЕ PASS; интерактивная UI-приёмка остаётся открытой.
- PowerShell Start-Process первоначально не сохранил exit codes завершённых
  процессов; поэтому короткие проверки повторены через System.Diagnostics.Process.
  Отдельная неудачная попытка запуска без plugin path остановлена; её процессы
  проверены по пути/времени и завершены, не выданы за результаты тестов.
- Staged --version прошёл с PATH только Windows/System32 и без QT_PLUGIN_PATH;
  зависимости загружены из пакета. Проверка выполнена до финализации.

Пакет: `artifacts/EI-Mapper-Ataman-2-first-test`. Только новый exe, новые
runtime-компоненты, Qt/MinGW deployment, проектные иконки/heightmaps, лицензии,
VERSION, один текущий CHANGELOG и краткий Readme. Игровые данные не копировались.
Упаковка: windeployqt --release --compiler-runtime --no-translations; явное
добавление Qt5Concurrent.dll, требуемой текущим валидатором.

Финализация PASS; удалён только workflow.log от проверки версии в собственной
staging-папке. Независимо проверены все 100 записей SHA256SUMS.txt: формат,
уникальность/границы путей, наличие файлов, полное покрытие и фактические хеши.
VERSION = 2, ровно один changelist; хеши exe и обоих runtime-компонентов
совпадают с выходом сборки. Размер файлов без манифеста: 76 215 179 байт.
SHA256 exe: `C4CBF4DC2FC90ABA4F484CF8639A5EA0687E6608AFB46ECD916AB1890FFF1FB5`.
После финализации пакет не запускался и не изменялся.

Статус: тестовый пакет готов к передаче с открытой UI-приёмкой.
Сборка предназначена для тестирования с указанным UI-пробелом, не для объявления
готовности релиза. GUI/игра не запускались, ZIP не создавался, main не менялся.
