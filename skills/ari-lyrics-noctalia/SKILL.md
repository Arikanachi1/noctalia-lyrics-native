---
name: ari-lyrics-noctalia
description: Use when building, testing, installing, or diagnosing the Ari Lyrics native backend with Noctalia and Hyprland, including missing cards, fullscreen detection, plugin reloads, and runtime versus source mismatches.
---

# Сборка и интеграция Ari Lyrics

Работай из корня `noctalia-lyrics-native`. Прочитай `README.md` и `CMakeLists.txt`: здесь C++20 / Qt6 Core, DBus, Network; интерфейс Noctalia находится отдельно в runtime.

## Сборка и проверки

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

Если существующий `build` ссылается на другой исходный каталог или toolchain, используй новый каталог сборки вместо удаления чужих артефактов. Зависимости проверяй по ошибке CMake; установка пакетов не является частью обычной проверки.

| Проверка | Что подтверждает |
| --- | --- |
| `lyrics-core` | LRC, совпадение записей, часы, допуск источников, fullscreen, ранжирование |
| `mpris-integration` | Исполняемый backend, отдельная D-Bus сессия, тестовые сокеты Hyprland, кеш и JSON |
| Runtime Lua и lint | Адаптер, окна, IPC и анимация Noctalia; CTest этого не проверяет |

CTest запускает интеграцию через `dbus-run-session`; реальный плеер и интернет ей не нужны. Если окружение запрещает D-Bus или локальные сокеты, сообщи конкретную ошибку и непроверенный участок. Успех одной цели не означает успех всей связки.

## Если карточка пропала

Сопоставь JSON из `build/ari-lyrics --snapshot` с ожидаемым источником: `musicEligible`, `playing`, `fullscreenKnown`, `fullscreen`, затем `state`, `timingAvailable`, `position`, `line`. Первый снимок может предшествовать ответу Hyprland и загрузке текста. Для наблюдения переходов используй поток обычного запуска backend и заверши только запущенный для диагностики процесс.

`fullscreen.cpp` читает `.socket.sock` и `.socket2.sock` из `$XDG_RUNTIME_DIR/hypr/$HYPRLAND_INSTANCE_SIGNATURE`, с резервом `/tmp/hypr/…`. Неизвестное состояние скрывает карточку. Бит `2` поля `fullscreen` означает настоящий fullscreen; режим `1` — максимизация и не блокирует карточку. Учитывается активное окно специального рабочего стола. При асинхронных ответах проверяй ревизию события: старый ответ не должен вернуть карточку после нового fullscreen-события.

Кеш хранится в `~/.cache/ari-lyrics/tracks/`, ручной выбор — в `~/.config/ari-lyrics/overrides/`, поправки — в `~/.config/ari-lyrics/offsets.json`. Для воспроизведения используй временные XDG-каталоги по образцу `integration.cpp`; очистка пользовательских данных не нужна для обычной диагностики.

## Runtime и установка

README указывает runtime `$HOME/.local/share/noctalia/plugins/ari-lyrics`. Сначала проверь его наличие и состав. `cmake --install` устанавливает только `bin/ari-lyrics`; он не создаёт Luau-адаптер или описание плагина.

Если задача включает установку, после успешной сборки и тестов выполни:

```sh
cmake --install build --prefix "$HOME/.local/share/noctalia/plugins/ari-lyrics"
noctalia msg plugins disable ari/lyrics
noctalia msg plugins enable ari/lyrics
```

Эти команды изменяют runtime и перезагружают плагин; выполняй их в рамках запроса на установку, с учётом доступных прав среды.

Если в runtime доступны соответствующие файлы, оттуда запускаются `lua verify-service.lua`, `lua verify-search.lua`, `lua verify-motion.lua`; декларативный интерфейс проверяется `noctalia plugins lint`. Не создавай отсутствующие runtime-проверки по одному упоминанию в README. В отчёте раздели подтверждённые сборку/CTest, установку и работу живого интерфейса.
