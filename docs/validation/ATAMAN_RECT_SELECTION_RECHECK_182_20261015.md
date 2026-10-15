# Повторный дефект выделения в Ataman182

2026-10-15. Поручение: снова внедрить прежнюю правку, исключавшую выбор объектов вне рамки. После первоначального исследования владелец уточнил: в установленной182 снова выбираются посторонние объекты. База main4a6847a7431b8b77ee134f8f6cac6a3649818fac, VERSION182. Задача открыта, исправление не заявляется.

## Установленные факты

Прежний scoped-коммит deec5d79165d4da8b4da2f7712dff63abb1cfea4 (Ataman116) найден. Его shader alpha cutoff, UV, per-part primary/secondary texture, очередь texture-aware, selectionReadRegion и чтение RGB сохранены. Сравнение с116: select.fsh, vshader.vsh, object_base.cpp, part.cpp, render_queue.cpp/h и selection_region.h без изменений. CSelect дополнительно получил собственный anchor и защиту от чужого release; нормализованная QRect сохранена. Повторное применение старого патча само по себе ничего не восстановит.

Установленный EXE182 и свежий проверенный build-release-182-properties/release/EIMapperEndurance.exe имеют одинаковый SHA256 D7472C0ED48104B88B06CE89C7B00E7965206E62BEFEDE167AFC8C01BC4EEDA0. Исправление панели свойств182 присутствует.

## Проверки текущего кода

- selection_render_tests:42 assertions PASS,0 FAIL, native desktop OpenGL. Прозрачность/alpha25/26, координаты четырёх направлений, границы viewport, immediate/instanced/разные текстуры/UV/глубина.
- render_queue_tests:15 PASS,0 FAIL/SKIP.
- Дополнительный диагностический helper связан с production objects свежей182 через существующий native entrypoint. Реальный CView::pickObject, синтетический CObjectBase с прозрачной текстурой, реальные CPart/очередь/встроенные shaders:7 PASS,0 FAIL. Контроль margin/center/outside с GL_DITHER включённым и выключенным. Настройки и cwd изолированы, игровые данные не загружаются, окно не показывается.
- Эти PASS не опровергают наблюдение владельца и не доказывают исправление его сценария. Реальная карта/модель и жест пока не установлены; запрошены уточнения. Журнал текущей сессии показывает182 и zone3obr, но не записывает состав выбора и координаты рамки.

Evidence: build-selection-review-182-20261015/{selection_render_tests.out,render_queue_tests.out,candidate-native.out,result.json}. Sandbox запуск toolchain отказал DLL relocation; проверки выполнены вне sandbox. Ошибки компиляции вспомогательного helper (недостающие заголовки/вызовы QGLFunctions) исправлены только в тесте, до итогового успешного запуска.

## Следующий шаг

Получить воспроизводимый пример: карта, модель/объект, рамка или клик; при возможности снимок с областью и лишним выбранным объектом. Проверить полный путь mouse gesture → ID buffer → сопоставление ID → нормализация сборки на этом примере и исправить первое неверное состояние. До него production-код, VERSION, установка, карты и recovery не изменены; новую сборку с якобы восстановленным патчем не выдавали.
