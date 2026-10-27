# EI-NAVMESH-AI-PANEL-229 — 2026-10-27

База main86b950a86a9ca7e32a55fe5928f5f073095df2d2/VERSION228. Реализация229 в main. СТОП ПЕРЕД УСТАНОВКОЙ, без push.

Причина228: MainWindow вызывает createNavmeshDock до CView::attach, где initAiGraphPreviewWidget создаёт панель. Условие if(m_aiGraphPreviewWidget) ложно; кнопки Navmesh в реальной панели вообще нет. Предыдущие harness отделяли CView от MainWindow и не проверяли видимость этой кнопки.

Единая Навигация ИИ: выбор источника, общий H, адаптивный размер/прокрутка и сворачивание. Старый граф сохранён. Navmesh: восемь профилей, статус, заливка/контуры, узкие места/острова, bake/cancel/progress/log, выбор сохраняемого пути запекателя. Отдельного дока, пустого поля пути и неактивной .navsrc нет.

Проверки/четыре снимка production MainWindow и Release/staging/finalizer: IN PROGRESS. Тестовые данные изолированы; установка не изменяется. Навыки private-engine/modding отсутствуют; приватный код движка не читается, vendor19 не изменяется. Используются обычный код маппера и прямо разрешённые fixture r13.
