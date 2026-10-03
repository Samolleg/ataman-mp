# Ataman105 — комплект распространения,2026-10-03

Задача EI-DISTRIBUTION-105-20261003. Владелец запросил версию для распространения. Ветка main, база документации `9f032ee9ae65e016889f99554786f2863f6309c5`; код105 `40ba71a781c64c17ef7d6e1761e1912d41d1c592`. VERSION105 и исходники не менялись: упакованы уже собранные и проверенные бинарники EI-NID-FAMILY-105. Повторная компиляция и новые функциональные прогоны в этой упаковочной задаче не заявляются.

## Результат

Чистая запускаемая папка:
`C:/GOG Games/maped/ei-mapper-ataman/artifacts/EI-Mapper-Ataman-105-distribution`.
129 файлов по manifest плюс SHA256SUMS.txt:130 файлов,82579828 байт. Есть Qt/MinGW DLL, Windows plugins, два runtime-компонента, иконки, лицензии/авторство, переводы Qt, синтетические heightmaps, README, VERSION и один текущий CHANGELOG105. Добавлены START_HERE_RU.md и docs/OBJECT_ASSEMBLIES.md. Русская инструкция объясняет перенос всей папки, первый запуск, собственный профиль игры/мода, витрину/Ctrl/Shift, пакетное размещение/Undo/Redo, ручной перенос готового каталога, «Рандом» и ограничения.

Нет игровых MOB/MPR/RES, пользовательского model-catalog.json, настроек, журналов, сохранений, дампов, compiler debris и ZIP. Игровые ресурсы получатель предоставляет отдельно. Публикация репозитория не выполнялась.

## Проверка пакета

Исходный clean build-staging-105-nid прошёл независимый validator. Только его127 manifest-listed файлов скопированы в новую собственную build-staging-105-distribution-20261003, пути/хэши проверены; не копировались unlisted данные постоянной установки. Документы добавлены до финализации, current CHANGELOG дополнен известными границами функциональных проверок.

Raw/staged editor и оба runtime-компонента byte-identical:

- EXE SHA256 `2085EDC28A07FCBDEB9C3CADEFC3EC3144F90AE980EAACFF84635670727AC36E`.
- JmvBridge SHA256 `725AC9E6D28B8F3BB64DAE891828DD04AD61CF71F95089E67752D2F0322B7A0C`.
- RuntimeHost SHA256 `5364535B2F497BD9DE62AD5DB440ACAE884B0C4D006B9A16E976752114472873`.

Скрытый staged --version до финализации:EI Mapper Endurance Edition105,exit0. Finalizer PASS; удалён созданный этим пробником workflow.log. Независимый stage validator PASS. После проверки абсолютного containment и отсутствия назначения новая finalized-папка перемещена в artifacts/EI-Mapper-Ataman-105-distribution; отдельный final validator PASS,129 SHA256 записей и полное покрытие payload. После финализации EXE не запускался. Интерактивная GUI/игровая приёмка получателя не выполнена.

Доказательства: `build-tests-105-distribution-20261003/package.ps1`,result.json,package.log,version.out/.err,validate-source.log,finalize.log,validate-stage.log,validate-distribution.log.

## Постоянная установка и сохранность

`artifacts/EI-Mapper-Ataman-2-first-test` уже105. Все127 managed файлов совпали с проверенным source105 до и после упаковки; manifest неизменен. Избыточная замена/очистка пользовательской установки не проводилась. Резерв104 сохранён. Endurance и игровые ресурсы не изменялись; чужие11 строк ROADMAP, отчёт52, QM и untracked86 не включаются в scoped-коммит документации этой задачи.

## Функциональные границы и следующий шаг

Использованы прежние результаты именно этих неизменных бинарников105:36 семейных проверок PASS; production143 IDs/Undo/Redo/атомарность, gallery/random/profile/database/hierarchy/group PASS. Синтетическое family-aware preview143 на1451 объектах1160мс. Полный --ui-widget-check exit1 с10 известными clipboard-отказами, новых нулевых флагов относительно104 нет. Подробности: ATAMAN_NID_FAMILY_105_20261003.md. Эти отказы не переобозначены PASS.

Упаковка завершена. Получатель копирует всю папку, запускает EIMapperEndurance.exe и выбирает собственную игру/мод. Реальные ресурсы, первое холодное чтение тяжёлых моделей, рабочая карта/сохранение и ручные приёмки102/103/104/105 остаются отдельными проверками; комплект не закрывает эти родительские задачи.
