# Отчёт этапа 0 — Xen Milenium

На этапе 0.1 дерево перенесено в `XenMilenium/`. Корневой `CMakeLists.txt` снова совпадает с TetraOP на `origin/master` (`c3f5b4f`). Текст ниже описывает этап 0 до этого переноса. Актуальные команды сборки — в `BUILD.md`.

## 1. Путь к новому проекту

`/workspace`

Чекаут не пустой и это не XEN 2000. До каркаса корень принадлежал продукту TetraOP (`PRODUCT_NAME` `TetraOP`, коды `Tilr` / `t4op`, bundle `com.tilr.tetraop`). Идентификаторов `Xen0`, `Xn20`, `com.XEN.XEN2000` в дереве не было.

По правилу этапа 0 проект создан в корне репозитория, на ветке `cursor/xen-milenium-stage0-7388`. Корневой `CMakeLists.txt` теперь собирает только Xen Milenium. Исходники TetraOP в `src/`, `libs/`, `doc/` не редактировались и в цель Xen Milenium не входят.

## 2. Созданные файлы

Заменён:

- `CMakeLists.txt`

Добавлены:

- `Source/PluginProcessor.h`
- `Source/PluginProcessor.cpp`
- `Source/PluginEditor.h`
- `Source/PluginEditor.cpp`
- `Source/Engine/README.md`
- `Source/Parts/README.md`
- `Source/Oscillators/README.md`
- `Source/Filters/README.md`
- `Source/Modulation/README.md`
- `Source/Effects/README.md`
- `Source/Presets/README.md`
- `Tests/README.md`
- `Tests/SilenceAndMidiSmoke.cpp`
- `Tests/check_vst3_identity.py`
- `Docs/PROJECT_RULES.md`
- `Docs/BUILD.md`
- `Docs/ROADMAP.md`
- `Docs/STAGE0_REPORT.md`

Каталоги движка содержат только короткие README. Пустых классов партий, осцилляторов, фильтров, модуляции, эффектов и окончательного GUI нет.

## 3. Версии инструментов

Команды запускались в `/workspace`.

- ОС: `uname -a` → `Linux cursor 6.12.94+ #1 SMP PREEMPT_DYNAMIC Fri Oct 16 00:53:04 UTC 2026 x86_64 GNU/Linux`
- CMake: `cmake --version` → 3.28.3
- C-компилятор сборки: `gcc --version` → gcc 13.3.0 (`Ubuntu 13.3.0-6ubuntu2~24.04.1`). CMake определил его как GNU 13.3.0.
- C++-компилятор сборки: `g++ --version` → g++ 13.3.0. Флаги цели содержат `-std=c++17`.
- Clang 18.1.3 установлен (`clang++ --version`), в сборку не входил.
- Ninja: `command -v ninja` не найден. Генератор — Unix Makefiles.
- Visual Studio / MSVC / `msbuild`: нет. Это Linux, Windows-toolchain отсутствует.
- JUCE до конфигурации в системе не был собран. `libs/JUCE` чекаута пустой. Подключение: FetchContent официального `https://github.com/juce-framework/JUCE.git`, тег `9.0.4`, коммит `3fb5cbc0340758596dac39f7c8de47d562b4896e`.
- VST3: отдельный Steinberg SDK не ставился. Используется VST3 из JUCE. `moduleinfo.json` сообщает `SDKVersion` `VST 3.8.0`.
- Пакеты, найденные при конфигурации: alsa 1.2.11, freetype2 26.1.20, fontconfig 2.15.0, pkg-config 1.8.1. `libxi-dev` установлен после первой ошибки компиляции (`X11/extensions/XInput2.h`).

Идентификаторы плагина, по правилу четырёх символов из `docs/CMake API.md` JUCE 9.0.4 (для AU: у производителя хотя бы одна заглавная, у плагина ровно одна; GarageBand 10.3: первая заглавная, остальные строчные):

- производитель `Xmnl` = `0x586d6e6c`
- плагин `Xmil` = `0x586d696c`
- bundle `com.xenmilenium.xenmilenium`
- имя `Xen Milenium`
- компания `XenMilenium`

Это не `Xen0`, не `Xn20` и не `com.XEN.XEN2000`.

## 4. Команда сборки

Каталог сборки: `/workspace/build-xen-milenium`.

```bash
cmake -S /workspace -B /workspace/build-xen-milenium -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
cmake --build /workspace/build-xen-milenium --config Release --target XenMilenium_VST3 -j2
```

`COPY_PLUGIN_AFTER_BUILD` = OFF. В системные каталоги VST3 результат не копировался.

## 5. Результат Release-сборки

Успех. Последний запуск `cmake --build ... --target XenMilenium_VST3 -j2` завершился кодом 0, в журнале: `[100%] Built target XenMilenium_VST3`.

До этого были две неуспешные попытки, обе исправлены до этой сборки:

- нет `JuceHeader.h` — добавлен `juce_generate_juce_header`;
- нет `X11/extensions/XInput2.h` — установлен `libxi-dev`.

Артефакт — VST3 с именем `Xen Milenium`, производитель `XenMilenium`, категории `Instrument` и `Instrument Synth`, `IsSynth=1`, `WantsMidiInput=1`. CID классов заканчиваются на байты `586D6E6C586D696C` (`Xmnl` + `Xmil`).

## 6. Путь к собранному VST3

`/workspace/build-xen-milenium/XenMilenium_artefacts/Release/VST3/Xen Milenium.vst3`

Бинарник: `Contents/x86_64-linux/Xen Milenium.so` (ELF 64-bit x86-64, 12620608 байт).

Метаданные: `Contents/Resources/moduleinfo.json`.

## 7. Какие проверки реально выполнены и чем кончились

- Осмотр окружения: чекаут — TetraOP, не XEN 2000. Поиск `*.vst3` и следов XEN 2000 в `/usr`, `/opt`, `/home`, `/workspace` до сборки ничего не нашёл.
- Конфигурация CMake: код 0. JUCE 9.0.4 скачан FetchContent-ом в каталог сборки.
- Release-сборка цели `XenMilenium_VST3`: код 0.
- `python3 Tests/check_vst3_identity.py /workspace/build-xen-milenium`: `identity ok`. Имя `Xen Milenium`, производитель `XenMilenium`, категории инструмента-синтезатора, ELF непустой. В `.so` есть строка `Xen Milenium` и нет `Xen0`, `Xn20`, `com.XEN.XEN2000`, `XEN 2000`, `XEN2000`.
- `XenMilenium_Smoke` после последней правки теста: сборка код 0, запуск напечатал `smoke ok`, код 0. Проверены отказ от моно и от входной шины, тишина после ненулевых и NaN-сэмплов, note-on 60, игнор velocity 0 и note-off, вторая нота 64, счётчик 2, пустой MIDI-блок, bypass и блок из 0 сэмплов.
- `ctest` в каталоге сборки после последней правки теста: `SilenceAndMidiSmoke` passed, 1/1, код 0. Прямой запуск того же бинарника напечатал `smoke ok`.
- `git diff --stat` по `src`, `libs`, `doc`, `README.md`, `.gitmodules`, `.github`, `install`, `resources`, `.gitignore`: пусто, эти пути не менялись.
- В `link.txt` и `build.make` цели VST3 нет путей TetraOP (`/workspace/src`), `libs/JUCE` и XEN 2000.
- `~/.vst3`, `/usr/lib/vst3`, `/usr/local/lib/vst3` после сборки по-прежнему отсутствуют. Установки нет.
- Загрузка в DAW не выполнялась.

## 8. Нерешённые проблемы

- Плагин в DAW не загружался. Такого прогона не было, успех загрузки не заявляется.
- Windows-сборка невозможна в этом окружении: нет Visual Studio и MSVC.
- Корень репозитория раньше собирал TetraOP. На этой ветке `CMakeLists.txt` заменён, поэтому TetraOP из этого корня больше не конфигурируется. Его файлы не редактировались и к Xen Milenium не подключены. Исходники TetraOP в дереве остаются.
- `libcurl`, webkit и EGL не установлены. Текущему каркасу они не нужны. Для браузера или curl их придётся ставить отдельно.
