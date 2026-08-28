# Contributing to EI Mapper Endurance through Codex

This guide is intended for contributors who do not need to be comfortable with
Git or the project internals. Codex performs the technical Git operations while
the contributor reviews the task, result, and test build.

## Simple working model

- `main` is the accepted and validated editor version.
- Your branch is an isolated workspace for one task.
- A Pull Request (PR) proposes adding your work to `main`.
- The maintainer reviews the PR and integrates acceptable changes.

Do not work directly on `main` or merge branches yourself. This protects the
stable version and allows a PR to be revised or rejected safely.

## One-time setup

1. Clone the GitHub repository or open an existing clone.
2. In Codex, create a project whose root is the `ei-mapper-endurance` folder,
   not its parent `Codex` folder.
3. Confirm that the contributor's GitHub access works.
4. Start a new Codex task for each new issue and paste the template below.

## Codex task template

Replace the angle-bracketed text and send the complete block to Codex:

```text
Work on EI Mapper Endurance in the open repository.

Read AGENTS.md and CONTRIBUTING.md completely and follow them. Do not work
directly on main. First fetch origin and create a dedicated branch named
contrib/<my-github-username>/<short-task-name> from current origin/main.

Task:
<precise description of one change or bug fix>

Before changing code, reproduce the original problem with a test that has an
explicit expected result and trace the first incorrect value or state. Do not
perform unrelated refactoring, formatting, or cleanup. Do not modify installed
game files, mods, private source, or user files; inspect them read-only only as
allowed by AGENTS.md.

After implementation, run proportionate tests. Commit only files belonging to
this task and push only your contributor branch to origin. Never push or merge
main and never force-push. Open a Pull Request when possible; otherwise report
the exact branch and full commit hash.

In the final report include:
- branch, full commit hash, and PR link;
- behavior and files changed;
- test/build results;
- required manual checks;
- every uncertain choice, assumption, and evidence gap, or explicitly state
  that there are none.

If origin/main conflicts, an implementation choice is materially ambiguous, or
unowned unfinished changes are present, stop and report it. Do not guess or
overwrite anything.
```

For example, contributor `ivan` working on object highlighting would use
`contrib/ivan/object-highlight`.

## What to send the maintainer

After completing the task, send:

1. the Pull Request link;
2. the branch name and full commit hash;
3. a short description of the result;
4. test results;
5. clear manual verification steps, when needed.

Do not resolve a difficult merge yourself. If the PR conflicts with `main`, ask
Codex to record the exact conflicts and send that report to the maintainer.

## Never add these to Git

- installed game or mod files;
- private source or information about its provenance;
- logs, dumps, saves, personal settings, or credentials;
- build directories, `.o` files, Makefiles, or temporary test artifacts;
- large RES/MOB/MPR or other game data without explicit maintainer approval.

---

# Как безопасно работать над EI Mapper Endurance через Codex

Это руководство рассчитано на участника, которому не обязательно хорошо знать
Git или устройство проекта. Codex выполняет технические Git-операции, а человек
проверяет задачу, результат и тестовую сборку.

## Простая модель работы

- `main` — принятая и проверенная версия редактора.
- Ваша ветка — отдельное рабочее место для одной задачи.
- Pull Request (PR) — предложение добавить вашу работу в `main`.
- Владелец проекта проверяет PR и сам интегрирует подходящие изменения.

Не работайте напрямую в `main` и не объединяйте ветки самостоятельно. Это
защищает стабильную версию и позволяет без риска отклонить или доработать PR.

## Однократная подготовка

1. Клонируйте репозиторий с GitHub или откройте уже существующий клон.
2. В Codex создайте проект, корнем которого является именно папка
   `ei-mapper-endurance`, а не родительская папка `Codex`.
3. Убедитесь, что GitHub-доступ участника работает.
4. Для каждой новой задачи начинайте новый Codex task и вставляйте шаблон ниже.

## Шаблон задания для Codex

Замените текст в угловых скобках и отправьте весь блок своему Codex:

```text
Работай над EI Mapper Endurance в открытом репозитории.

Полностью прочитай AGENTS.md и CONTRIBUTING.md и соблюдай их. Не работай
напрямую в main. Сначала выполни fetch origin и создай отдельную ветку
contrib/<мой-github-логин>/<короткое-название-задачи> от актуального origin/main.

Задача:
<точное описание одного изменения или исправления>

Перед изменением кода воспроизведи исходную проблему тестом с явным ожидаемым
результатом и найди первое неправильное значение/состояние. Не делай
несвязанный рефакторинг, форматирование или очистку. Не изменяй установленную
игру, моды, приватные исходники и пользовательские файлы; исследовать их можно
только для чтения в рамках правил AGENTS.md.

После реализации запусти подходящие тесты. Закоммить только файлы этой задачи и
отправь только свою ветку в origin. Никогда не push/merge в main и не используй
force-push. Создай Pull Request, если это возможно; иначе дай точную ветку и
полный hash коммита.

В финальном отчёте укажи:
- ветку, полный commit hash и ссылку на PR;
- что изменено и какие файлы затронуты;
- результаты тестов/сборки;
- что нужно проверить вручную;
- все сомнительные решения, допущения и пробелы в доказательствах (или явно
  скажи, что их нет).

Если возникнет конфликт с origin/main, неоднозначный выбор реализации или
обнаружатся чужие незавершённые изменения, остановись и сообщи об этом — не
угадывай и ничего не перезаписывай.
```

Например, ветка для участника `ivan` и задачи с подсветкой объектов:
`contrib/ivan/object-highlight`.

## Что отправить владельцу проекта

После завершения пришлите владельцу:

1. ссылку на Pull Request;
2. имя ветки и полный commit hash;
3. краткое описание результата;
4. результаты тестов;
5. понятные шаги ручной проверки, если они нужны.

Не нужно самостоятельно разбираться со сложным merge. Если PR конфликтует с
`main`, попросите Codex зафиксировать точные конфликты и передайте их владельцу.

## Запрещено добавлять в Git

- установленные файлы игры или модов;
- приватные исходники и сведения об их происхождении;
- логи, дампы, сохранения, личные настройки и ключи;
- каталоги сборки, `.o`, Makefile и временные тестовые артефакты;
- большие RES/MOB/MPR или другие игровые данные без явного разрешения владельца.
