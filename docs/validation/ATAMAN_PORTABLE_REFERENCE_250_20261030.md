# Ataman250 — автоматический эталон рисования

Поручение30.10: текстурный пак из выдаваемого ZIP должен подключаться без ручного выбора пути. База main989f01bc/249; реализация main3a18941acc07bc41c62e90e25e57c5d05aa63f68. Чужие ROADMAP, translations/ei_mapper_ru.qm и игровые данные сохранены.

`CTextureList::terrainAuthoringArchivePath` использует общий resolver: доступный явно заданный файл имеет приоритет; иначе проверяется `terrain-reference/HDPack_Terrain-JamaisVu135-50c285225fb1745e.res` относительно `QCoreApplication::applicationDirPath()`. При отсутствии обоих сохраняется прежний пустой/недоступный путь. Настройки, пользовательский RES и пак мода не перезаписываются. Алгоритмы рисования/Undo/Save не менялись.

Native Qt `tests/terrain_reference_path_check.cpp`:8 PASS. Проверены пустой/пробельный путь, потерянная старая установка, приоритет доступного внешнего файла, перенос папки вместе с паком, отсутствие комплектного пакета и сохранение прежнего поведения без fallback. Использованы синтетические файлы в QTemporaryDir, исходники RES не нужны. Первоначальный qmake в sandbox завершился DLL relocation; тот же штатный Qt/MinGW вне sandbox собрал и выполнил тест. Системные настройки не менялись.

Fresh Release из Git-экспорта в build-source-portable-reference-250; staging/ZIP/install receipts хранятся в work/portable-reference-250-20261030. Завершение передачи фиксируется ниже. Ручная визуальная/игровая приёмка и работа на других GPU этим тестом не доказаны.

## Передача завершена

Fresh Release250 и runtime PASS; raw/staged four binary hashes совпали, staged --version250/exit0. Оба финализатора PASS:186→186 program и186→188 distribution,0 removed. ZIP artifacts/EI-Mapper-Ataman-250-with-textures.zip:520479444 bytes,SHA256 A5BE78B53D54CC80842DB65A814769B6F945B63E3F0D1BD1662C86B0FA1C13F2;189 entries, все188 payload hashes и manifest проверены в ZIP. Эталон точный50c285225fb1745ee70d280e57a214f346481e6d380a78d55475f730d11a81b2; персональные настройки в ZIP отсутствуют.

Штатный install_program обновил постоянную249→250:186 managed SHA PASS,8 замен,0 добавлений/удалений. Пак terrain-reference и пользовательские данные вне manifest не заменялись. Резерв249 artifacts/rollback249-to250-portable-reference-20261030; install.log. Первый CIM preflight в sandbox недоступен; повтор штатного installer вне sandbox PASS, защиты не обходились. Интерактивный редактор/игра не запускались.
