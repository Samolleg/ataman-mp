# МАЛАХИТ в Ataman: прототип237

Это адаптер существующего Malachite.UI.Wpf, а не отдельная копия его исходников.
Ataman создаёт native Qt-площадку; собственный .NET-процесс создаёт в ней дочерний
HwndSource. DPI-контекст берётся у родителя до создания WPF-окна. Основа Win32/WPF:
https://learn.microsoft.com/en-us/dotnet/desktop/wpf/advanced/wpf-and-win32-interoperation

Текст передаётся через UTF-8 JSON lines по redirected stdin/stdout. Синхронные
запросы module-contract Apply/Undo/Redo/Save обслуживает Qt; отдельный reader
хоста получает ответы, не блокируя канал WPF-диспетчером. Применение сверяет
редакцию и Windows-1251. Документ, его общая история и безопасная запись MOB
остаются в CView/CMob. Полный MOB-контейнер в WPF не передаётся.

Окно скрипта использует компонент, если рядом установлен
`tools/Malachite/Ataman.MalachiteHost.exe`. Для изолированных проверок есть
неперсистентный EI_MALACHITE_HOST. При отказе процесса исходный текст/доставленный
черновик возвращается в штатный редактор. Закрытие окна останавливает только
принадлежащий ему companion. EOF также завершает companion.

Нужен установленный .NET Desktop Runtime9 x64. В этом этапе один сценарий на
окно; открывать/сохранять MOB нужно через маппер. Службы БД, каталога карт,
объектных подсказок и внешнего ScriptCheck не подключены. Доступны код и его
встроенный статический анализ; компиляция движком и игровая приёмка не заявляются.
Навигация панели сценария маппера в код сохранена. Физические клавиши/IME,
переходы между мониторами и ручная DPI-приёмка проверяются владельцем отдельно.

Сборка из действующего бинарного модуля (без пересборки/изменения редактора БД):

```powershell
./tools/build_malachite_host.ps1 -ModuleSdk '<host SDK>' `
  -MalachitePackage '<binary module>' -OutputDirectory '<fresh Release/tools/Malachite>' `
  -BuildDirectory '<owned intermediate folder>'
```

`module-provenance.json` фиксирует SHA входных Core/UI/SDK и manifest донора.
Содержимое donor manifest не является manifest этого адаптера: модульный
загрузчик редактора БД здесь не используется. Сборка маппера обычная;
stage_release.ps1 добавляет companion из свежего Release/tools/Malachite.
Игровых ресурсов и исходников движка в пакете адаптера нет.

Адресная проверка — tests/malachite237_check.pro с production-объектами свежего
Release; запуск tests/run_malachite237_check.ps1 с отдельными каталогами состояния.
Команды edit/render/crash разрешены хостом только с --verify. Проверяются native
родитель, hidden/неактивный прогон, реальный TextBox, общая история, Save/reopen,
неизменность остальных секций, отказ CP1251/записи и восстановление черновика.
