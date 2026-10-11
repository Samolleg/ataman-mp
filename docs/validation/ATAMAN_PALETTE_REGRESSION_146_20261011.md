# EI-PALETTE-REGRESSION-146 — исследование 2026-10-11

Статус: сопровождающий подтвердил сохранение режимов143/144: Classic Auto
показывает7 кистей, Modern82 материала; переключение — «Современное рисование (Z)».
В146 исправлены только подтверждённые дефекты банка. Регресс, чистая Release,
финализация и установка145→146 завершены; передача зафиксирована отдельным docs-коммитом.

## База и границы

- main `e5148973b6b367ccfeac615ae426bd049ce70ba3`, VERSION/установка145.
- AGENTS SHA256 `f4b7fdd5ea82fcdf43d002d23d64ec36be853650f054edf70376527cd6432ec3`;
  прочитаны PROJECT_CONTRACT, TEST_BUILD_HANDOFF, отчёты145/141/137.
- Изменения программы и VERSION ограничены146; живые карты/паки и Endurance не изменены.
  Нет сети, установки зависимостей, системного clipboard или интерактивного GUI.
- Частный снимок81 файлов: Dejavu/Base MPR и Dejavu/JamaisVu HDPack;
  SHA исходника до = копия = исходник после. `build-tests-146/snapshot.json`.
- Копия текущего пользовательского банка: manifest/pages с проверкой SHA,
  current.json неизменен между чтениями; `owner-bank-snapshot.json`.
  Все81 исходных SHA его manifest совпали со снимком. В тестовой копии manifest
  заменены только пути на собственные копии с теми же SHA. Банк владельца не менялся.
- Hidden desktop, собственные settings/temp/backup, UI под ui-tests.lock.
  Во всех законченных прогонах clipboard guard PASS, nativeAttempts=0.
  Приватные engine/modding skills недоступны; приватный engine-код не читался.

## Проверка исходной предпосылки

Старая MPR из приватного снимка144 имеет SHA256
`d6c3445f22d5fd50bc2a53498cdf616e0c06d07cd1cffc0c5ee00dd3186bb1ff`.
Это точное совпадение с workflow-20261011-161701.log, событием open16:10:32.
Старый namespace `basegipat_213430e77bcf`, 448slots/7страниц128; новый
`basegipat_0c2dfc8150c3`, 512slots/8страниц, 7HD +1SD. В обоих профилях
445 описанных слотов и восемь локальных семейств.

До-HD открывается с совместимым старым HDPack из снимка144
SHA256 `b3b123dde10513cf6dcc40d60ff4a9e21177017a51989b991c2445eb3f0868cf`.
Полное побайтное тождество всего этого RES паку именно в16:10 не установлено;
тождество MPR и соответствие её собственного опубликованного атласа проверены.
После-HD использует свежий снимок живого RES. Открытие старой MPR с новым RES
не является валидным сравнением: прежний namespace уже удалён.

Production-linked harness использует соответствующие source headers, main
и все production objects Release143/144/145, заменяя только точку входа
автоматическим read-only аудитом. Количество и идентичности берутся из
реальных CView/CLandscapeTools, а не реконструируются из логов.

| Программа | Classic Auto до HD | Classic Auto после HD | Modern до / после |
|---|---:|---:|---:|
|143|7|7|81 /81|
|144|7|7|82 /82|
|145, без доступного банка|7|7|82 /82|
|145, варианты выключены|7|7|82 /82|
|145, копия действующего банка ON|7|7|82 /82|
|145, копия действующего банка OFF|7|7|82 /82|

ON добавляет22 generated choices к семи пунктам Classic; OFF даёт0. Он не
заменяет семь пунктов. Полный Modern145: Grass13, Ground20, Stone18,
остальные31. Состав Classic: Grass/Common, Dark, Irregular; Ground/Common;
два Ground/Water stamps; Stone/Lime.

**Заявленная регрессия числа пунктов после HD/145 не воспроизведена.**
Нельзя честно предъявить требуемый FAIL145→PASS146 на условии «после HD столько
же материалов, сколько до»: на этих идентичных MPR это условие уже истинно.
Различие режимов доказано; какой режим был выбран владельцем в более раннем
визуальном наблюдении, не установлено. Смена режима владельцем — не доказанный факт.

Evidence: `audit-143-{before,after}/audit.json`, аналогичные144/145,
`bank-palette-summary.json`, собственные runner/compile receipts в build-tests-146.
Для окончательных PNG145 harness дожидается m_terrainGlobalHydrationPendingJobs=0;
ранние PNG с многоточиями не являются окончательной визуальной проверкой.

## Точные причины наблюдаемого поведения

1. `CView::refreshClassicAutoPalette` (source/render/view.cpp) фильтрует полный
   Modern-каталог по `catalog.paintVariants(pure)` именно текущей карты.
   Дополнительно он создаёт stamps для сочетаний1-на-3. Эта реализация есть
   и в143/144. В данном профиле пять обычных кистей и два контекстных stamp.
   `displayOnly:true` относится к GL-изображению смешанных native страниц,
   а не к доступности/количеству материалов.
2. `Ground / Water`, ID1557947799, не имеет uniform signature в bz1g.
   Classic добавляет два cornerOnly/contextualStamp: support Stone/Lime
   и Grass/Common. `terrainVertexJunctionThumbnail` собирает preview2×2
   из четырёх исходных переходных тайлов, водяными углами к центру. Поэтому
   видна вода в центре. Это не новый однородный тайл, созданный банком145.
   Сравнение OFF подтверждает независимость этой миниатюры от банка.
   `terrain_edit_model.cpp` отказывает с указанным пользователем текстом,
   когда не может представить центральную вершину доступными authored
   сочетаниями. У corner-only stamp нет произвольной однородной внутренней
   области. Точные координаты40 мазков из пользовательской сессии не восстановлены.
3. Независимый дефект банка подтверждён: в действующем algorithm1 банке
   `d0e3a6ae8391f8e93417e962b6c668bdddbefdbcf68952b6e47a6dc3a5ab6ebc`
   101группа/350вариантов. Включены `Ground / Water|0|1|land` и
   `Ground / Water|1|1|land`, каждая по5 новых; доноры соответственно
   zonemainmenunew:61 и zone1:79 с одинаковым pixel SHA
   `7659cd4cf1725d87bb3e051a695a68898b08e784d5039f6f6ee26314b56dccc2`.
   Генератор проверяет равенство четырёх углов/непрозрачность/анимацию и
   исключает только имена, начинающиеся `Water /`. Ground/Water проходит.
   Полное raw evidence: `owner-bank-audit.json`; другие underwater/lava/swamp
   группы там перечислены, их художественная классификация не подменена догадкой.
4. Ключ и load-проверка банка действительно хешируют полные RES и MPR из
   Config, включая их пути: `identity`, `cacheIdentity`, `current` в
   source/render/terrain_hd_variant_bank.cpp. Поэтому изменение несвязанной
   публикации меняет ключ/актуальность. Runtime guard дополнительно отвергает
   изменение size/mtime. Это исходное поведение145, исправленное в146.

## PNG

- [Classic до HD,145](../../build-tests-146/bank-145-before-on/palette.png).
- [Classic после HD,145](../../build-tests-146/bank-145-after-on/palette.png).
- [Modern до HD,145](../../build-tests-146/bank-145-before-on/palette-modern.png).
- [Modern после HD,145](../../build-tests-146/bank-145-after-on/palette-modern.png).
- [Ground/Water на Grass, после HD](../../build-tests-146/bank-145-after-on/stamp-1557947799-2142727678.png).

- [Classic146, варианты ВКЛ](../../build-tests-146/bank-146-after-on/palette.png).
- [Modern146, варианты ВКЛ](../../build-tests-146/bank-146-after-on/palette-modern.png).
- [Classic146, варианты ВЫКЛ](../../build-tests-146/bank-146-after-off/palette.png).

PNG146 просмотрены: у двух водяных contextual stamps отсутствуют стрелки меню
сгенерированных однородных вариантов. Classic:7 пунктов и12 вариантов обычных
материалов; OFF:7 и0. Modern:82 при ON/OFF. JSON-списки Classic/Modern с OFF
точно совпали с144, до и после HD число исходных материалов сохраняется.

## Решение и реализация146

По решению сопровождающего режимы палитры не меняются. Classic7 штатен,
Modern82 доступен через «Современное рисование (Z)». Контекстные кисти сохраняются;
их меню больше не предлагает сгенерированные однородные варианты.

Алгоритм банка повышен до2. uniformDonor требует подтверждённое selectable-семейство,
четыре одинаковых угла, отсутствие ambiguity, route owner, mixed-edge recipes и
анимации. Оба класса (reviewed/physical) должны быть статичными: water6, lava13,
swamp14 исключены. Семантическое слово Water/Lava также исключает донор, в том
числе Ground / Water с физическим классом Ground. ID материалов не зашиты.
Статические донные грунты с классом Ground и именем Underwater не приравниваются
к водной поверхности; Grass / Swamp с классом Grass также остаётся сухим грунтом.
Тот же фильтр защищает load, forMaterial и import. Одна регенерация банка145 нужна
из-за версии алгоритма; установщик сохраняет старый банк и не запускает генерацию.

Ключ использует SHA payload фактически выбранных stock MMP и точных embedded
профилей, версию алгоритма, SHA пресетов и упорядоченные идентичности доноров с
классами/слоями. Пути, size/mtime и SHA целых RES/MPR исключены. Значения MP-классов
фиксируются в группах при генерации; изменяемые MPR не являются live-зависимостью.
При изменении size/mtime RES available перечитывает зависимые members. Это проверка
необходимости перечитать payload, а не причина сбрасывать банк.

На снимке:96 групп,326 вариантов,14 допустимых shortfalls,1873 rejected candidates,
68 input members. Обе Ground / Water группы удалены. Новая идентичность:
`9ad4fd32085d97f9746b13ab56b8a88739eab8335420426dabe479ecd2f78de8`.

## Проверки146 и передача

Ожидаемый отрицательный тест145:2 PASS /1 FAIL /0 SKIP (Ground / Water ошибочно
предлагался как uniform variant). Тот же тест146 с проверкой общего donor rule:
4 PASS /0 FAIL /0 SKIP. Evidence: terrain_hd_variant_bank_regression_tests/{before145,after146}.txt.
Полный инженерный регресс:

| Проверка | Результат |
|---|---|
| Variant kernel synthetic | 594812 PASS,9485 BC1 blocks,585216 edge pixels |
| Полный корпус96 групп; повторная генерация каждого | 45882893 PASS;618422 BC1 blocks;45256704 edge pixels;0 FAIL |
| Bank integration | 11 PASS /0 FAIL /0 SKIP |
| HD republish | 10 PASS |
| Terrain atlas / resolve / world sculpt | 146 /5 /48 PASS |
| Mixed resolution | 4 PASS |
| Recipe | 17451 PASS;18394 edges;2396804 RGBA comparisons |
| Полный UI | 233 PASS, nativeAttempts=0 |
| Scenario / dark-theme | 49 /70 функциональных PASS плюс clipboard guard |
| Model gallery | 497 моделей /7165 текстур, PASS |
| Random136 / mixed-edge139 | 80 /67 PASS,0 FAIL,0 SKIP |

Во всех GUI-прогонах nativeAttempts=0 и clipboard unchanged=1.
Bank integration:97 из144 покрашенных ячеек используют варианты; смешанные
переходы13 ячеек изменены, shuffle20 ячеек реально отрисованы. После обычного
save/reopen/HD сохраняются5 generated identities и все BC1 mip-данные;18 страниц.
OFF:прямое сравнение с production objects144 —1041760 одинаковых байт секторов
(`variants-off.sec` / `variants-off-144.sec`). Старый helper пишет метку «145 OFF»,
но reference-файл в этом прогоне создан именно integration146.

Актуальность подтверждена тремя независимыми воздействиями:
1. В unit test несвязанный RES member меняет размер/offsets — load/available PASS;
   payload донора меняется при сохранённых размере RES/mtime — load отказывает.
2. Production CLI переводит другую синтетическую карту128→256:32 HD applied,
   31 animated slots защищены;4 published members добавлены в копию настоящего
   HDPack объёмом1286206772 байта. Load банка до/после успешен, manifest/ID
   побайтно прежние, генерация не вызывалась. `publication-{receipt,bank}.json/out`.
3. В настоящем используемом `basegipat000.mmp` изменён один байт payload:
   load отказал. В finally исходный байт восстановлен, SHA payload снова
   `6c8add8ae9b491f1d9b4f556b280c6dca9ac64bbe4db7a31c5042e4ff8dd62c5`,
   load снова успешен. `actual-stock-change.json`, `actual-stock-{invalidated,restored}.out`.
   Все воздействия только на build-tests-146/copies, живые данные не изменялись.

Миниатюры145→146:5 чередующихся пар,80 измерений, в каждом8/8 готовы,
unavailable0, cold8 вызовов, warm/repeat0. Медианы cold fill:characters99.118→91.185ms
(-8.00%), buildings31.176→29.881ms (-4.15%). Все cold gates ≤10% либо+15ms PASS;
все восемь сравнений также PASS. `gallery-summary146.json` / `gallery-raw146.json`.
База145 подтверждена SHA установленного EXE до обновления.

## Release и установка

- Ветка main. Scoped-коммит реализации:
  `f7f75bfa0ec389e662f3ddf9bee656ada75a7d0d`.
  Автор/коммиттер Codex <codex@localhost>, требуемый Co-Authored-By сохранён.
- Свежие `build-source-146-final` и `build-release-146-final` из git archive;
  SHA256 TAR `a2c186e4553dd0d1c826e88fc91f613533de35b9cb35d6a03e91b0745818ab5e`.
  488 source files идентичны проверенной инженерной версии; VERSION/resources
  также сверены. Wrapper exit0,102.672с, оба x86 runtime-компонента пересобраны.
- Свежий EXE --version146 и полный --ui-widget-check233 PASS, nativeAttempts0.
  Ранее успешный регресс неизменных исходников не повторялся без основания.
- Новая собственная `build-staging-146-final`: raw/staged EXE SHA равны,
  staged --version146 PASS до финализации. Finalizer exit0 и встроенный
  validate_distribution PASS;127 managed files. Удалён только workflow.log,
  созданный проверкой версии в этой staging. После финализации пакет не запускался.
- SHA256 EXE:
  `567a82d1959a91b454cbcf90cf80accf539caf7d1fb4d4694269663a229dcd2f`.
  SHA256 manifest:
  `72fbf7f9bac6fbab8ec871bbe0f79f8447ea77019eb6af5d9e379f6a2a571a7e`.
- [Постоянная установка](../../artifacts/EI-Mapper-Ataman-2-first-test) обновлена
  2026-10-11 22:51:53 +05:00. До установки в22:50:32 EIMapperEndurance.exe не
  запущен:0 процессов, ожидание0мин. Guards повторены перед записью.
- Заменены6 managed files: EXE, VERSION, Readme, CHANGELOG и два runtime-компонента.
  Manifest заменён последним, obsolete0. Все127 установленных файлов соответствуют manifest.
  `--version` из отдельной собственной cwd:146,exit0; интерактивного запуска не было.
- [Rollback145](../../artifacts/rollback145-to146-20261011):7 точных резервных файлов
  включая старый manifest, SHA проверены до записи. Старые rollback-каталоги
  не принадлежали этой задаче и сохранены; ни один rollback не включён в Git.
- 22709 пользовательских файлов и8 файлов единственного LocalAppData банка сохранены
  с прежними SHA; набор файлов также неизменен. Финализатор на установке не запускался.
  Старый algorithm1 банк сохранён: владелец должен один раз регенерировать его
  командой «Сгенерировать HD-варианты материалов…». Новая тестовая генерация не
  переносилась в пользовательский банк.
- Evidence: `package-receipt.json`, `release-{build-wrapper,version,ui}.json/out`,
  `finalizer.out`, `install-process-check.json`, `install146-receipt.json`,
  `install146-user-{files,banks}.xml` в build-tests-146.
- Передача оформлена вторым scoped docs-коммитом в main, без push. ZIP для обычной
  локальной тестовой установки не запрашивался и не создавался.

Ручная приёмка покраски/вида материалов владельцем остаётся отдельным этапом;
игра/мод/живые HDPack и MPR/Endurance не изменены. Сеть, зависимости и системный
clipboard не использовались. Обычный редактор за владельца не открывался.

Ошибки стенда не зачтены как PASS: первая компиляция143 обращалась к полю145;
первые открытия старой MPR получили неподходящий новый RES; первый кадр снят
до завершения фоновых миниатюр. Неверное имя CLI переключателя исправлено.
У QCore regression нет clipboard API: лишнее ожидание clipboard-строки удалено
после проверки успешного qtest-лога, сам успешный тест не повторялся.

Random136 сначала отказал до сценария: отсутствовала заранее созданная output-папка.
Создана только собственная папка стенда, повторён только этот незавершённый сценарий;
первичный FAIL сохранён как random-invalid-output.out/json.

Инженерная сборка из рабочего дерева первоначально вернула wrapper exit1 из-за
поиска vswhere.exe внутри VsDevCmd, хотя компоненты были собраны. Это не принято
как Release PASS: для чистой архивной сборки локальный PATH дополнен существующим
Visual Studio Installer; финальный wrapper завершился0. Глобальный PATH не менялся.
