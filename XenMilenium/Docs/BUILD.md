# Сборка Xen Milenium

## Требования

- CMake 3.22 или новее. В успешной сборке использован CMake 3.28.3.
- Компилятор C++17. В успешной сборке: gcc 13.3.0 и g++ 13.3.0 (`Ubuntu 13.3.0-6ubuntu2~24.04.1`). Clang 18.1.3 установлен, но в эту сборку не входил.
- Linux x86_64. Пакеты, которые понадобились: `libfreetype6-dev`, `libfontconfig1-dev`, `libx11-dev`, `libxcomposite-dev`, `libxcursor-dev`, `libxext-dev`, `libxinerama-dev`, `libxrandr-dev`, `libxrender-dev`, `libxi-dev`, `libasound2-dev`, `libglu1-mesa-dev`, `mesa-common-dev`, `pkg-config`.
- Сеть на конфигурации: JUCE скачивается через FetchContent.
- Visual Studio и другой Windows-toolchain в этом окружении нет. Windows VST3 здесь не собирался.

## JUCE

Использована версия **9.0.4**, коммит `3fb5cbc0340758596dac39f7c8de47d562b4896e`, тег `9.0.4`.

Источник: `https://github.com/juce-framework/JUCE.git`.

Подключение: `FetchContent` в `XenMilenium/CMakeLists.txt`, не локальная копия. Каталог `libs/JUCE` родительского чекаута — пустой подмодуль другого продукта и в сборку не входит.

Отдельный Steinberg VST3 SDK не устанавливался. Заголовки VST3 приходят вместе с JUCE. В собранном `moduleinfo.json` указано `SDKVersion` `VST 3.8.0`.

Стандарт проекта: C++17 (`-std=c++17`).

## Конфигурация и сборка

Каталог исходников: `/workspace/XenMilenium`. Его не нужно передавать в корневой CMake TetraOP.

Каталог сборки: `/workspace/XenMilenium/build-xen-milenium`. Это не каталог `build` TetraOP и не системный каталог VST3. `COPY_PLUGIN_AFTER_BUILD` выключен.

```bash
cmake -S /workspace/XenMilenium -B /workspace/XenMilenium/build-xen-milenium -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
cmake --build /workspace/XenMilenium/build-xen-milenium --config Release --target XenMilenium_VST3 -j2
```

Цель формата VST3 называется `XenMilenium_VST3`. Генератор по умолчанию — Unix Makefiles. Ninja не установлен.

До этой успешной пары команд были две неудачные попытки сборки: сначала не генерировался `JuceHeader.h`, затем не хватало `X11/extensions/XInput2.h`. Обе причины устранены до успешной сборки (`juce_generate_juce_header` и пакет `libxi-dev`).

## Куда кладётся результат

`/workspace/XenMilenium/build-xen-milenium/XenMilenium_artefacts/Release/VST3/Xen Milenium.vst3`

Внутри бандла:

- `Contents/x86_64-linux/Xen Milenium.so`
- `Contents/Resources/moduleinfo.json`

Проверка без DAW:

```bash
cmake --build /workspace/XenMilenium/build-xen-milenium --config Release --target XenMilenium_Smoke -j2
/workspace/XenMilenium/build-xen-milenium/XenMilenium_Smoke_artefacts/Release/XenMilenium_Smoke
python3 /workspace/XenMilenium/Tests/check_vst3_identity.py /workspace/XenMilenium/build-xen-milenium
```

## Ограничения

- Это каркас этапа 0: стереотишина, приём MIDI и временное окно. Осцилляторов, партий, фильтров, модуляции, эффектов, пресетов и окончательного GUI нет.
- Загрузка в DAW не проверялась: DAW в окружении нет.
- Сборка только Linux x86_64.
- `libcurl`, `webkit2gtk-4.0` и EGL при конфигурации не найдены. Эта сборка их не требует: `JUCE_WEB_BROWSER=0` и `JUCE_USE_CURL=0`.
- В `~/.vst3`, `/usr/lib/vst3` и `/usr/local/lib/vst3` плагин не устанавливался. Этих каталогов на машине нет.
