# Ataman204: установка и восстановление gipat1

2026-10-17, EI-NEW-MAP-204. Реализация source main `a0d52328d3b40e8ddd83863492b7771d67ab21c8`, версия204. Проверки: [412 native checks](ATAMAN_NEW_MAP_204_20261017.md).

- Fresh staging build-staging-204-new-map: raw/staged204, EXE hash equality, finalizer и complete-manifest PASS. Установлены130 managed файлов по договору, заменённых/убранных7, новых1. EXE SHA256 `B8AEF137C04A9B330268544B67D13F0C1C5345371380A6F2C51F883A8EE02EEE`, manifest `9D9675B51C21EBFCAF6387FF625D97B3F7F5C68099696BAB9EE5AFD6BD86054C`.
- Настройки, журналы, карты и прочие16852 unlisted user files сохранены побайтно; общий digest до/после `16852:90B897AED1B81FC46AAE2EFB28CA545E5842A1EC1361440E3FB324A62466E139`. Редактор и runtime из постоянной папки перед записью не работали. Финализатор на постоянной установке не запускался.
- Постоянная программа: artifacts/EI-Mapper-Ataman-2-first-test. Резерв203: artifacts/rollback203-to204-new-map-20261017. Восстановимость всех файлов203 проверена; удалён только точно опознанный прежний202→203 резерв согласно договору. Другие старые/чужие каталоги не затрагивались.
- В C:/GOG Games/PZ/MODS/DejaVu/Maps добавлен только gipat1-atlas.res. SHA256 `e214f17bca7d374d497e373f32aec2f5789b17828860729c55ceb161fc6001fe` совпал с проверенным native-кандидатом. Весь gipat1.mpr остался побайтно прежним, SHA256 `9adf9cff2a31f189c9a7108d9c0a746e71a4b8ef0e255efe882bae21babe3ec2`. Рельеф, размеры и содержимое SEC не переписывались. Резерв исходного MPR: build-new-map-204-20261017/rescue/gipat1.mpr.
- Receipts: build-new-map-204-20261017/{build-receipt,final-probe-receipt,staging-receipt,install-receipt,live-repair-receipt,rollback-retirement}.json. Исходники/артефакты на GitHub не отправлялись.

Результат выполнен. Пользователь может открыть gipat1 в установленной204. Ручная оценка изображения и игровая приёмка остаются владельцу; автоматические UI/GL проверки их не подменяют.
