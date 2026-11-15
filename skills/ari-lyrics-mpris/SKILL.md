---
name: ari-lyrics-mpris
description: Use when working on Ari Lyrics playback timing, MPRIS source selection, Seeked handling, LRC synchronization, or lyrics that freeze, jump, or follow the wrong song in noctalia-lyrics-native.
---

# MPRIS и синхронизация Ari Lyrics

Работай из корня `noctalia-lyrics-native`. Сначала прочитай разделы «Поведение» и «Данные и диагностика» в `README.md` и код затронутого участка.

## Где искать

| Задача | Код |
| --- | --- |
| Допуск Wave / YouTube Music, идентичность URL, часы | `playback.hpp`, `playback.cpp` |
| D-Bus, выбор плеера, смена песни, JSON | `main.cpp`: `activeTrack`, `poll`, `seeked`, `emitState` |
| LRC, текущая строка, нормализация и совпадение записи | `lyrics.cpp`, `lyrics.hpp` |
| Ручной выбор, ранжирование, поправки | `catalog.cpp`, `catalog.hpp` |
| Детерминированные проверки часов и парсера | `tests.cpp` |
| Полный цикл MPRIS → JSON | `integration.cpp` |

## Контракт времени

- D-Bus `Position`, `mpris:length` и `Seeked` передают микросекунды. Внутренняя позиция и LRC — секунды; `PlaybackClock` получает монотонное время в миллисекундах.
- Неизменный `Position` при `Playing` допустим: часы продолжают идти по `Rate`. Пауза замораживает позицию; продолжение и смена скорости сохраняют её непрерывность.
- `Seeked` сразу меняет опорную позицию. Следующее чтение старого `Position` не должно отменить перемотку. Обработчик сначала обновляет метаданные через `poll()`, поскольку сигнал может прийти уже для новой песни.
- Временная потеря длительности или позиции той же песни сохраняет подтверждённые часы. Новая идентичность сбрасывает их. Изменение playlist/query у того же YouTube Music video ID не означает новую песню.
- Без подтверждённого времени показывается обычный текст со статусом `untimed`. Автоматическое восстановление через паузу не предусмотрено; ручное восстановление относится к runtime Noctalia.
- Положительная поправка задерживает текст: `currentLine(lyrics, position - offsetSeconds)`. Ручной выбор с расхождением длительности более 3 секунд даёт `version_mismatch` без синхронизации.

## Допуск источников

Wave определяется парой DesktopEntry `wave` и Identity `Wave` с нормализацией из `playback.cpp`; его собственные аудиопотоки могут иметь YouTube URL. Для браузера разрешён точный host `music.youtube.com`. Обычные `youtube.com` / `youtu.be` исключаются даже при имени YouTube Music. Список специальных настольных приложений находится в `isYouTubeMusic`.

## Проверка изменения

Для ошибки часов добавь воспроизводимый сценарий в `tests.cpp` с явными отметками времени. Для гонки метаданных, сигнала или JSON используй `integration.cpp`: там отдельные D-Bus, сокеты Hyprland и кеш с оригинальными тестовыми строками. Проверяй ожидаемую позицию и строку, а не только отсутствие падения.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

В результате укажи воспроизводимый сценарий, изменённый контракт и фактически пройденные проверки. Для диагностики живого плеера `build/ari-lyrics --snapshot` выдаёт лишь первый снимок: загрузка текста и состояние Hyprland могут ещё не успеть прийти.
