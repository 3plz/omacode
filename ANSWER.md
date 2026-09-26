# Omarchy N-Legacy: техническое исследование и проверка архитектуры

Ответ на `ASK.md`. Дата исследования: **24 сентября 2026 года**.

## Как читать этот документ

Это исследование исходников, спецификаций и документации, а не отчёт об испытаниях GTX 650. В текущем рабочем окружении доступна Windows; указанная Linux-машина, её PCI inventory и реальные EGL/DRM capabilities не предоставлены. Поэтому успешный запуск, аппаратные extension strings, fps и устойчивость здесь **не выдумываются**.

Встроенный веб-поиск был недоступен. Первоисточники загружались напрямую с NVIDIA, GitHub, Khronos, kernel.org, freedesktop.org и Arch/AUR. Часть архивных URL оказалась недоступна; такие пробелы отмечены. Отрицательный результат скачивания не считается доказательством отсутствия функции.

Статусы утверждений:

| Метка | Значение |
|---|---|
| **VERIFIED** | Подтверждено указанной спецификацией, документацией или прочитанным кодом. Не означает испытание на вашей карте. |
| **VERY LIKELY** | Сильный инженерный вывод из нескольких подтверждённых фактов; аппаратная проверка ещё нужна. |
| **PLAUSIBLE** | Технически осмысленная гипотеза с конкретным проверяемым условием. |
| **UNKNOWN** | Доступных данных недостаточно; указан эксперимент, который решит вопрос. |
| **FALSE ASSUMPTION** | Предпосылка противоречит спецификации или проверенному коду. |

Проверенные срезы:

| Компонент | Срез исследования |
|---|---|
| Hyprland | `e368c13c27a42a173b9e08fa0bf413f9f7073187`, commit от 2026-09-23 |
| Aquamarine | `7bb8bdf4e8fedaf4dfae58512bc4c728671727df`, commit от 2026-09-22 |
| NVIDIA proprietary | Документация **470.256.02**; для сравнения GBM — **495.44** |
| egl-wayland | Исходники **1.1.7**, исторически релевантные 470; README и XML также сверены с текущей веткой |
| KWin | **v5.22.0**, `src/plugins/platforms/drm/egl_stream_backend.cpp` и `drm_output.cpp` |
| wlroots-eglstreams | `danvd/wlroots-eglstreams`, дерево `6ea541730e87ac8ce4187e55a0c3aa3cefef5624` |
| Omarchy | Прочитана ветка **dev**, дерево `b9ddccfc377abe0b8fc3ff1ee5b31a86bf202d4a`; это не обещание поведения любого установленного release |
| AUR | RPC и PKGBUILD показали `nvidia-470xx-{utils,dkms}` **470.256.02-8.03** |

Ссылки на исходники и документы приведены рядом с выводами; полный список — в конце. Ссылки на изменяемые ветки означают состояние при исследовании, а не вечную гарантию.

---

## PART 1 — Executive Summary

**В предложенной схеме найден реальный архитектурный разрыв: stock Hyprland не становится EGLStream-producing Wayland client только потому, что его запустили внутри EGLStream compositor.**

У проверенного Aquamarine Wayland backend собственный output swapchain. Он требует `zwp_linux_dmabuf_v1`, получает DRM device через dmabuf feedback, создаёт GBM allocator и публикует его buffers как linux-dmabuf `wl_buffer`. Этот путь не вызывает NVIDIA Wayland window-surface EGL integration для кадров рабочего стола. Объявление `wl_eglstream_controller` внешним сервером его не переключит. **VERIFIED.** [Aquamarine Wayland][aq-wayland], [Aquamarine core][aq-backend], [GBM allocator][aq-gbm].

NVIDIA 470 действительно документирует аппаратный вывод через EGLDevice/EGLStream/EGLOutput, но прямо исключает GBM allocation/submission для своего KMS. Поэтому внешний EGLStream output решает **последний** участок маршрута, а проблемы allocation/render/export **внутри Hyprland** остаются до него. [NVIDIA 470 KMS][nv-kms].

Более того, существует **второй независимый разрыв**: приложения подключаются к внутреннему Wayland display Hyprland. Если NVIDIA EGL-клиент требует EGLStream globals, находящиеся только на внешнем display N-Legacy, он их не увидит. Работающий compositor frame ещё не доказывает аппаратное ускорение приложений, Xwayland, браузеров и видеоплееров внутри Hyprland.

Итоговые решения:

1. **Не начинать большой compositor с обещанием stock Hyprland + аппаратный NVIDIA 470.** Сначала проверить наличие совместимого allocator → renderer → dma-buf producer path у самого Hyprland.
2. Сделать два маленьких независимых прототипа: **P0 — проверка входа Hyprland**, **P1 — native EGLStream output test**. Успех P1 не закрывает P0.
3. **Outer compositor на libwayland-server + GL + EGLOutput технически обоснован** для SHM/EGLStream-клиентов и для реально импортируемых dma-buf. Это ещё не доказанный мост для указанного Hyprland.
4. При неизменяемых ограничениях единственная содержательная обходная гипотеза на одной этой карте — **реальный Mesa/software producer с экспортируемыми buffers**, затем CPU upload или подтверждённый импорт в NVIDIA. Она требует отдельной проверки и не равнозначна аппаратно ускоренному современному desktop.
5. Наиболее прямые пути к стабильному современному Hyprland — совместимый GPU либо проверенный Nouveau/Mesa на этой карте. Первый меняет оборудование, второй — условие proprietary 470. Это альтернативы с явно изменёнными ограничениями.

**Ответ на вопрос «в принципе невозможно?»**: Wayland nesting вообще не запрещён, EGLStream output вообще не запрещён. Но конкретная идея «stock Hyprland сам выдаст EGLStream внешнему compositor» опровергнута кодом. Подтверждённого полноценного аппаратно ускоренного пути с одновременным выполнением всех исходных ограничений не найдено. Не следует превращать это в более сильное, недоказанное утверждение о невозможности любого software/virtual-device решения.

## PART 2 — Verified facts about NVIDIA 470 / GTX 650

### 2.1. Что известно об оборудовании и драйвере

GTX 650 присутствует в списке устройств NVIDIA 470.256.02. В нём встречаются как минимум device IDs `0FC6` и `11C8` с названием GTX 650; поэтому определение только по маркетинговому имени или одному ID недостаточно. Фактический ID ASUS-карты нужно получить через `lspci -nnk`. **VERIFIED.** [Supported chips][nv-chips].

Xeon E3-1230 v2 не следует рассматривать как запасной Intel graphics renderer: эта модель не предоставляет встроенную графику. Наличие видеовыходов на motherboard само по себе не создаёт GPU. Перед проектированием multi-GPU fallback всё равно снять полный PCI inventory; отдельная установленная карта могла бы изменить вывод.

470 — закрытый NVIDIA stack с kernel interface, зависящим от версии ядра, и согласованным vendor userspace. Современные NVIDIA open kernel modules не являются заменой для Kepler/470. Успешная сборка DKMS означает совместимость сборки kernel interface, но не добавляет GBM backend в закрытую EGL implementation. [Состав драйвера][nv-components], [AUR package][aur-package].

### 2.2. Capability matrix: не путать уровни

| Feature | Доказанное о 470 | Что нужно проекту / сложность |
|---|---|---|
| EGLDevice / device DRM mapping | **VERIFIED**, документированный output path | Сопоставить EGLDevice именно с нужным DRM device; не брать первый в списке |
| EGLStream | **VERIFIED** как семейство используемых API | Отдельно проверить producer, GL consumer, cross-process и output extensions |
| EGLOutput | **VERIFIED** в KMS-документации | Проверить реальные layers, mapping к plane/CRTC и modeset на данном kernel |
| `EGL_WL_bind_wayland_display` | **VERIFIED** механизм в egl-wayland; runtime на конкретном EGLDisplay — **UNKNOWN** | Extension + загруженный external platform + успешный bind обязательны |
| `EGL_WL_wayland_eglstream` | **VERIFIED** используется KWin/NVIDIA integration | Не универсальный Wayland core API; нужна совместимая NVIDIA реализация |
| NVIDIA native GBM backend | **VERIFIED: отсутствует** в документированном 470 KMS path | Главный барьер для Aquamarine allocator |
| PRIME/dma-buf вообще | **VERIFIED:** NVIDIA документирует PRIME | Это не доказательство всех комбинаций import/export/render/scanout |
| `EGL_EXT_image_dma_buf_import` | **UNKNOWN** для конкретной установленной 470 + EGLDisplay + форматов | Даже наличие extension не гарантирует render-target или scanout usage |
| `EGL_EXT_image_dma_buf_import_modifiers` | **UNKNOWN** для стенда | Нужны результаты format/modifier queries и реальные импорты |
| `EGL_MESA_image_dma_buf_export` | **UNKNOWN**, нельзя положить в базовый контракт 470 | Наличие EGLImage или GL texture не доказывает экспорт |
| `GL_TEXTURE_EXTERNAL_OES` + stream consumer | **VERIFIED** архитектурно в историческом KWin | Проверить GL consumer extension и корректный shader; это не обычная 2D render target |
| EGLImage | **VERIFIED** механизм используется graphics stack | Поддержка конкретного image target, siblings и export независима |
| EGL/GL fences | **VERY LIKELY** базовые fence paths; точный набор — runtime probe | Fence object внутри EGL не обязательно экспортируется в Linux FD |
| `EGL_ANDROID_native_fence_sync` | **UNKNOWN** для стенда; не baseline | Query и import/export roundtrip; старый driver нельзя приравнивать к современному |
| DRM syncobj / timeline | **UNKNOWN** у 470 DRM device | Новое ядро не реализует driver capabilities за драйвер |
| Atomic KMS | **VERIFIED**, NVIDIA документирует | Наличие atomic не доказывает `IN_FENCE_FD`, `OUT_FENCE_PTR`, HDR/VRR |
| Legacy KMS / dumb buffers | **VERIFIED** dumb path; конкретные modeset операции проверить | Полезный независимый CPU test |
| Primary/cursor planes | **VERIFIED** в README 470 | Размеры cursor, formats и импорт query runtime |
| Overlay planes | **VERIFIED: README 470 говорит, что не регистрируются** | Не строить overlay promotion архитектуру |
| Presentation timing | Есть исторический NVIDIA flip-event path | Правильные timestamps и callback integration — отдельная задача |
| Buffer age / swap with damage | В egl-wayland есть соответствующая логика | Конкретные extensions и корректность после resize/reconfigure проверить |
| Direct scanout | **UNKNOWN** для стороннего конкретного buffer | EGLStream output и dma-buf direct scanout — разные механизмы |
| VRR / HDR / color management | **UNKNOWN** как end-to-end путь на этом стеке | Не включать в первую версию; capabilities GPU сами по себе недостаточны |
| Multi-monitor | **VERIFIED** исторический per-output design; аппаратная устойчивость — **UNKNOWN** | Отдельные streams, clocks и lifecycle на каждый output |
| Hotplug / suspend / resume | Есть документированные/исторические механизмы | Нужны hardware tests на выбранной связке kernel + patchset |

Основания таблицы: [NVIDIA KMS][nv-kms], [egl-wayland hooks][egl-exports], [KWin][kwin-egl], [NVIDIA power management][nv-power]. Строки UNKNOWN преднамеренны: публичный список Khronos extensions не является дампом вашей NVIDIA EGL implementation.

### 2.3. GBM существовал раньше NVIDIA GBM

Это два разных исторических факта. GBM — Mesa API, а NVIDIA 495.44 документирует собственный GBM backend и GBM EGL external platform. В 470.256.02 README прямо сказано, что allocation/submission через GBM не поддерживается. Следовательно, тезис **«поддержка proprietary NVIDIA GBM появилась в ветке 495, позднее 470» подтверждается сравнением официальных веток**. Здесь не устанавливается точная дата первого beta-коммита. [495 GBM][nv-gbm].

Установка современного `libgbm.so` рядом с 470 добавляет frontend API и Mesa implementation, но не превращает NVIDIA allocator в backend 495. Возможные Mesa dumb/software paths исследуются отдельно; это не NVIDIA native GBM.

### 2.4. Роль kernel и границы UAPI

DRM/KMS ioctls, GEM/PRIME, dma-buf и sync_file относятся к Linux userspace-facing interfaces: существующий UAPI должен сохранять совместимость. Но стабильность интерфейса не означает наличие каждой operation у каждого driver. Даже capability bits — только первый фильтр; реальный import/usage test обязателен.

Внутренний kernel API, против которого собирается NVIDIA module interface, не имеет той же стабильности. Поэтому DKMS patches решают изменения типов, callbacks и внутренних interfaces; они не заменяют закрытый renderer/allocator. NVIDIA `/dev/nvidia*` interactions и vendor EGLStream extensions образуют дополнительный driver-specific contract.

Legacy modesetting и EGLStream не следует называть исчезнувшими только потому, что современный compositor выбрал другой path. Но исторический API без поддерживаемого producer, package compatibility и hardware tests недостаточен для production. [DRM UAPI][drm-uapi], [NVIDIA components][nv-components].

## PART 3 — Verified facts about modern Wayland / Hyprland / Aquamarine

### 3.1. Реальная цепочка nested output

В проверенном исходнике:

1. Hyprland запрашивает Aquamarine implementations HEADLESS, DRM и WAYLAND с разными request modes.
2. `CWaylandBackend::start()` вызывает `wl_display_connect(nullptr)`.
3. Backend связывает `wl_compositor` v6, `xdg_wm_base` v6, `wl_seat` v9, `wl_shm` v1, `zwp_linux_dmabuf_v1` v4.
4. Отсутствие любого из этих globals либо ошибка dmabuf init приводит к `Missing protocols`/ошибке запуска.
5. `get_default_feedback` → `main_device` → `drmGetDeviceFromDevId()` → выбор render node, иначе primary node → `open()`.
6. `CBackend::start()` создаёт `CGBMAllocator`, используя доступный DRM FD. Для обычных backend’ов отсутствие allocator фатально.
7. GBM BO выделяется под output swapchain. Из него запрашиваются plane FDs, pitches, offsets и modifier.
8. `CWaylandBuffer` создаёт linux-dmabuf params, добавляет planes и вызывает `create_immed`.
9. Output делает attach, damage, frame request, commit и flush.

Это **VERIFIED по функциям**, а не вывод из общей репутации Hyprland. [Compositor initialization][hypr-compositor], [Wayland backend][aq-wayland], [allocator][aq-gbm], [backend core][aq-backend].

### 3.2. Важное уточнение про EGL

Неверно говорить, что renderer Hyprland всегда требует именно `EGL_PLATFORM_GBM`. Проверенный `CHyprOpenGLImpl` умеет выбрать **EGLDevice platform**, сопоставленный с DRM FD, и имеет GBM platform fallback. Контекст пробует GLES 3.2, затем 3.0. Однако отдельный Aquamarine allocator и dma-buf output contract от этого не исчезают. Возможность создать NVIDIA EGLDevice context — только один из необходимых шагов. [Hyprland OpenGL][hypr-gl].

Следовательно, точное препятствие:

```text
не просто eglInitialize()
а совместимая последовательность:
GBM allocation → export planes → EGL import/render target
→ корректная synchronization → linux-dmabuf delivery
```

Нельзя исправить её только серверной реализацией `wl_eglstream_controller`.

### 3.3. Что stock nested backend не обещает

| Возможность | Что видно в проверенном backend |
|---|---|
| EGLStream output desktop frame | Нет такого пути; output идёт через linux-dmabuf |
| SHM desktop fallback | Нет в `CWaylandBuffer`; SHM реализован для cursor path |
| Present feedback | `onFrameDone()` создаёт упрощённый present event; физические `wp_presentation` timestamps не проходят полноценно |
| Refresh | Начальная mode description использует 60000; это не измерение физического refresh |
| Formats | Код отмечает, что использует общую format table вместо полного корректного учёта tranche formats |
| Input | Есть keyboard/pointer; полнота touch/tablet/relative-pointer/pointer-constraints не следует из этого кода |
| Parent output discovery | В рассмотренном registry handler нет полноценного mapping физических `wl_output` в nested outputs |
| Headless | Есть, но его `drmFD()` возвращает `-1`; headless сам не создаёт пригодный GPU allocator |
| X11 backend | В проверенном наборе Aquamarine implementations отсутствует |

Это ограничения **проверенного среза**, а не прогноз о всех будущих версиях. Например, исправления frame scheduling и configure в текущей ветке уже показывают, почему версия backend должна быть частью test matrix. [Wayland][aq-wayland], [Headless][aq-headless].

### 3.4. Backend selection и environment

`WLR_BACKENDS`, `WLR_RENDERER`, `WLR_DRM_DEVICES` — не интерфейс настройки текущего Aquamarine только потому, что они применялись к старым wlroots compositor’ам. Не выдавать `WLR_BACKENDS=wayland` за проверенную команду запуска современного Hyprland.

`AQ_DRM_DEVICES` действительно читается DRM backend, но это список DRM devices, **не переключатель в software mode** и не полноценный selector Wayland backend. `WAYLAND_DISPLAY` указывает parent socket; `XDG_RUNTIME_DIR` должен принадлежать пользователю. `WAYLAND_SOCKET`, если унаследован, способен изменить способ подключения libwayland и должен контролироваться launcher’ом.

Запуск должен проверять по логам, что выбран нужный backend, FD и allocator. Не принимать ситуацию «Hyprland незаметно запустился на доступном другом DRM output» за успех bridge.

### 3.5. Две границы совместимости

```text
NVIDIA 470 application
    │  boundary A: приложение → Hyprland
    ▼
stock Hyprland
    │  boundary B: Hyprland → N-Legacy
    ▼
N-Legacy
    │  boundary C: N-Legacy → физический display
    ▼
GTX 650
```

Нельзя закрыть A реализацией protocol globals только на B. Wayland registry принадлежит конкретному соединению. Добавление внешнего EGLStream server не внедряет его buffer importer в Hyprland. Проверка Firefox/Chromium/GTK/Qt/GLX/Vulkan/Xwayland внутри Hyprland обязательна независимо от first-frame. [NVIDIA external platform][egl-readme], [Hyprland protocols][hypr-protocols].

## PART 4 — Buffer architecture

### 4.1. Это не один объект с разными именами

| Термин | Что это | Кто держит ресурс / чего он не доказывает |
|---|---|---|
| `wl_buffer` | Wayland protocol object, представляющий content resource | Client/server имеют protocol state; тип backing storage определяется creator protocol |
| SHM buffer | `wl_buffer`, описывающий область `wl_shm_pool` | Память CPU, offset/stride/format; не GEM и не автоматически dma-buf |
| linux-dmabuf buffer | `wl_buffer` из `zwp_linux_buffer_params_v1` | Planes + FD + layout metadata; готовность GPU отдельно |
| dma-buf FD | Descriptor на kernel shared-buffer object | Process-local FD number; refs дублируются через FD passing; это не указатель на pixels |
| GEM handle | Handle buffer object в контексте DRM file | Не переносим как число в другой process/DRM open; не обязательно новый allocation |
| DRM framebuffer ID | KMS object: dimensions, format, handles, pitches, offsets, modifiers | Представление backing objects для scanout; не сам allocation |
| GBM BO | Userspace allocator object | Может владеть BO/FD; не обязательно scanoutable/renderable/exportable во всех комбинациях |
| EGLImage | EGL image sharing object / источник image siblings | Не универсальный container, умеющий преобразовать всё во всё |
| EGLStream frame | Image, доступное в producer/consumer queue | У обычного GL consumer нет публичного fd/stride/modifier на каждый frame |
| GL texture | GL object с sampling/storage semantics в share group | GLuint нельзя передать через Wayland и использовать в другом процессе |
| EGLSurface | EGL rendering/presentation endpoint | Window, pbuffer или stream-producer surface; не единственный вечный framebuffer |
| Scanout buffer | Allocation, который display engine способен читать в выбранной конфигурации | Свойство конкретного device/plane/mode/format, а не самостоятельный универсальный тип |
| Physical display surface | Разговорное имя видимого результата | В API — connector/CRTC/plane, mode, timings и последовательность scanout; не переносимый объект |

Основание: [Wayland core][wayland-core], [linux-dmabuf][proto-dmabuf], [DRM UAPI][drm-uapi], [EGLStream][spec-stream].

### 4.2. Три независимых вида владения

Следует разделять:

1. **Lifetime reference** — allocation ещё существует, потому что его удерживают FD, GEM/FB или GL/EGL references.
2. **Право записи/повторного использования** — producer может снова писать только после окончания всех relevant consumers.
3. **Protocol lifetime** — `wl_resource` ещё жив и ему можно отправить событие.

`close(fd)` может уменьшить lifetime ref, но не означает «GPU закончил». `wl_buffer.release` может разрешить reuse, но не уничтожает client-side allocation. Уничтожение `wl_buffer` клиентом не отменяет уже отправленную работу GPU и не даёт серверу право разыменовывать умерший `wl_resource`.

### 4.3. Реальные цепочки

**DMA-BUF direct scanout:**

```text
producer allocation
  → export dma-buf FD + metadata
  → FD passing + linux-dmabuf wl_buffer
  → DRM PRIME import on target drm_fd
  → GEM handles on that DRM file
  → AddFB2[WithModifiers]
  → atomic TEST_ONLY / real commit
  → scanout
  → later replacement/disable completed
  → consumer releases this usage
```

FD passing и PRIME import обычно создают новые references, а не копию пикселей. Но driver migration/internal copies нельзя исключить только по API trace.

**GL composition из dma-buf:**

```text
linux-dmabuf wl_buffer
  → eglCreateImageKHR(EGL_LINUX_DMA_BUF_EXT, metadata)
  → GL texture sibling
  → draw into compositor-owned output target
  → EGLStream output OR GBM/KMS output
```

Здесь **client buffer никогда не обязан стать DRM framebuffer**. Он может быть sampleable и совершенно непригодным для scanout. **Ответ на вопрос №1: да, N-Legacy может показать неимпортируемый KMS buffer через GL, если именно GL/EGL умеет его импортировать.** Если ни EGL, ни CPU не способны прочитать источник, «GPU blit fallback» отсутствует.

**EGLStream composition:**

```text
NVIDIA EGL client → stream-backed wl_buffer
  → server-side EGLStream handle
  → GL_TEXTURE_EXTERNAL_OES consumer
  → acquire → sample in fullscreen draw → release
  → separate output EGLStream producer surface
  → EGLOutput layer → scanout
```

Промежуточный публичный `EGLImage` в этой цепочке **не обязателен и не предоставляется автоматически**. Нельзя считать EGLStream frame и EGLImage взаимозаменяемыми. [GL consumer specification][spec-gl-consumer], [KWin stream texture][kwin-egl].

**SHM composition:** `wl_shm` → validated CPU bytes → texture upload → GL draw → output. Это отличный независимый диагностический маршрут, но не существующий SHM output mode у рассматриваемого Aquamarine.

## PART 5 — EGLStream architecture

### 5.1. Stream — договор между producer и consumer

EGLDevice выбирает GPU; EGLDisplay задаёт EGL namespace и реализацию; EGLStream связывает producer с consumer. Producer может быть EGLSurface, куда GL рисует и где `eglSwapBuffers()` публикует новый image. Consumer может читать images как external texture либо представлять их через EGLOutput. У stream выбранный consumer; нельзя после подключения GL consumer считать тот же stream одновременно независимым output consumer. [EGLStream][spec-stream], [EGLOutput consumer][spec-egloutput].

Базовые stream states:

```text
CREATED → CONNECTING → EMPTY
                       ↕
               NEW_FRAME_AVAILABLE
                       ↕
               OLD_FRAME_AVAILABLE
                       ↓
                  DISCONNECTED
```

Это states самого stream, а не полный lifetime graph каждого allocation. `DISCONNECTED` требует прекращения обычного потока и восстановления endpoint; переподключение — обычно создание нового stream generation.

Mailbox допускает замену ещё не потреблённого содержимого более новым. FIFO сохраняет очередь с bounded capacity и backpressure. FIFO extensions, synchronous FIFO и acquire mode меняют детали блокировок; название «FIFO» само по себе не задаёт весь контракт. Consumer acquire связывает frame с consumer texture; release возвращает frame stream machinery с требуемой синхронизацией.

**Существенная деталь:** `eglStreamConsumerReleaseKHR()` для GL consumer защищает предшествующие GL-команды, использующие acquired frame. Поэтому безусловный `glFinish()` перед каждым release не является требованием EGLStream. Но эта гарантия не распространяется магически на чужие GL contexts, посторонние dma-buf writers или доступ CPU. [GL consumer][spec-gl-consumer].

### 5.2. Cross-process transport: да, но не любой producer

`EGL_KHR_stream_cross_process_fd` предоставляет FD, представляющий **stream endpoint**, а не dma-buf image. Он используется для создания сопряжённого EGLStream handle в другом процессе. Передавать такой FD в `drmPrimeFDToHandle()` неверно. Дополнительные NVIDIA stream-socket transports — отдельные варианты, не обязательные для local prototype. [Cross-process FD][spec-cross-process], [NVIDIA stream XML][egl-stream-xml].

Таким образом:

| Вопрос | Ответ |
|---|---|
| Может ли вообще nested compositor быть EGLStream producer? | Да, если его presentation code реализует этот путь |
| Обязательно ли приложение знает proprietary protocol? | Нет: обычный Wayland EGL window client может получить его через NVIDIA external platform |
| Вызывает ли современный Aquamarine этот window-surface путь для output? | Нет, проверенный backend сам создаёт dma-buf `wl_buffer` |
| Может ли N-Legacy включить этот путь только своими globals? | Нет |
| Можно ли stream FD трактовать как image FD? | Нет |

### 5.3. Что делает `libnvidia-egl-wayland.so`

Это EGL external platform library: интеграция NVIDIA EGL с Wayland native display/window. Она загружается через external-platform JSON. Это **другая** роль, чем GLVND vendor JSON для `libEGL_nvidia.so`; путать эти два loader layers нельзя. NVIDIA 470 описывает библиотеку как client-side Wayland support поверх EGLDevice/EGLStream. [NVIDIA components][nv-components].

При этом утверждение «в ней совсем нет server-side части» тоже неверно. В 1.1.7 есть `wayland-eglstream-server.c`, display bind hooks и hooks для `eglCreateStreamAttribNV`/`eglQueryWaylandBufferWL`. Библиотека помогает связать `wl_display` и EGLDisplay, создавать stream-backed resources и интерпретировать их. Она **не выполняет** за compositor shell policy, scene composition, input focus, output scheduling и controller consumer attachment. [Server implementation][egl-server], [hooks][egl-exports], [display integration][egl-display].

### 5.4. `wl_eglstream_controller`: точный смысл

Участвуют минимум `wl_display`, registry, `wl_surface`, stream-backed `wl_buffer`, `wl_eglstream_display` и `wl_eglstream_controller`. Сам controller — server-advertised global, requests к нему отправляет client integration. XML v2 добавляет consumer attachment attributes, включая preferred present mode и FIFO length. [Controller XML][egl-controller].

Типичная последовательность NVIDIA Wayland EGL-клиента:

1. Client создаёт `wl_surface`, проходит shell configure и создаёт `wl_egl_window`/EGL window surface.
2. External platform создаёт client-side stream и получает transport FD.
3. `wl_eglstream_display.create_stream` создаёт `wl_buffer`, описывающий stream connection.
4. `attach_eglstream_consumer[_attribs]` сообщает серверу, к какой surface относится stream и какие consumer preferences запрошены.
5. Сервер получает EGLStream через NVIDIA Wayland EGL integration, делает current нужный GL context, создаёт external texture и подключает consumer.
6. Client integration завершает handshake и подключает producer surface. **Consumer connection нельзя отложить до кадра, которого producer без consumer ещё не может выпустить.**
7. Client рисует, делает swap; integration отправляет attach/damage/commit. В некоторых режимах уведомления отправляет отдельный damage thread после stream frame notification.
8. Server query/acquire проверяет реальное состояние stream и потребляет доступный frame. Сам Wayland commit не является GPU fence.

По исходникам 1.1.7 один и тот же stream-backed `wl_buffer` может повторно attach’иться для новых frames. Это не обычная модель «один новый `wl_buffer` на каждый image». На resize создаются новые surface/stream resources; старые контексты удерживаются до stream-resource release. Поэтому посылать `wl_buffer.release` на каждый acquired stream frame как разрешение уничтожить transport — опасное смешение двух lifetimes. [Surface lifecycle][egl-surface], [swap][egl-swap].

Damage — информация о содержимом surface, а не описание memory layout. Resize меняет image dimensions и требует нового согласованного generation. Destruction должен различать disconnect, release acquired frame, destruction GL consumer и destruction protocol resource. Не копировать из старого кода небезопасные casts/цикл по `wl_array` без проверки длины и alignment.

### 5.5. EGLOutput — presentation endpoint, а не dma-buf exporter

Документированный путь 470:

```text
DRM master + connector/CRTC/plane configuration
    → EGLDevice matching this DRM device
    → EGLDisplay with EGL_DRM_MASTER_FD_EXT
    → EGLOutput layer selected by DRM plane/CRTC
    → stream with this output consumer
    → EGL stream-producer surface
    → GL rendering + swap
```

EGL не заменяет обнаружение connectors, выбор mode, EDID, session ownership и hotplug policy. Исторический KWin использует dumb FB для modesetting, EGLStream для реального rendered content и NVIDIA acquire/flip-event integration для presentation. **Dumb FB здесь не следует считать копией кадра compositor.** [KWin][kwin-egl], [KWin DRM output][kwin-drm], [EXT_device_drm и EXT_output_drm][spec-device-drm].

Auto-acquire output может быть проще для статической картинки. Для управляемого frame pacing нужен проверенный механизм представления и событий; исторический `EGL_NV_output_drm_flip_event`/`EGL_DRM_FLIP_EVENT_DATA_NV` — vendor-specific dependency, которую надо явно проверять, а не представлять как обязательную часть Khronos core.

## PART 6 — dma-buf / DRM architecture

### 6.1. Правильная последовательность

1. Получить FDs и все metadata, проверить protocol invariants и limits.
2. Определить unique dma-buf objects: несколько planes могут ссылаться на один объект с разными offsets.
3. На **целевом** DRM FD вызвать `drmPrimeFDToHandle()` для нужных objects. Полученные handles действуют в namespace этого DRM file.
4. Сформировать массивы `handles[4]`, `pitches[4]`, `offsets[4]`, `modifiers[4]`; unused entries обнулить.
5. Использовать `drmModeAddFB2WithModifiers()` с корректным flags для explicit modifier либо соответствующий implicit-layout API path. Нельзя просто стереть неподдерживаемый modifier.
6. Проверить geometry, plane `IN_FORMATS`, routing и plane properties. Составить atomic state, выполнить TEST_ONLY, где это применимо.
7. Установить framebuffer, CRTC/connector/mode и fences, затем real commit.
8. Держать scanout usage до завершения замены/disable, а не до возврата submit syscall.

**Успех PRIME import не равен успеху AddFB; успех AddFB не равен успеху atomic commit; успех TEST_ONLY не гарантирует, что изменившееся к real commit состояние hardware ещё доступно.** [DRM UAPI][drm-uapi].

### 6.2. Metadata, которые нельзя потерять

| Данные | Почему нужны |
|---|---|
| width/height | Размер image, не обязательно размер allocation |
| DRM FourCC | Channel interpretation, plane organization, subsampling |
| plane count и plane index | Соответствие format + modifier; auxiliary planes могут отличаться от простого RGB/YUV представления |
| FD/object identity per plane | Разные planes могут делить один memory object |
| offset | Начало plane внутри backing object |
| stride/pitch | Расстояние между строками, не `width × bpp` по умолчанию |
| 64-bit modifier | Tiling/compression/layout contract конкретного vendor/формата |
| flags/orientation | Например, Y inversion; transform не выводится из FD |
| color semantics | FourCC недостаточен для всей colorimetry, transfer function, range, alpha conventions |
| acquire dependency | Когда producer завершит запись |
| usage requirements | Sampleable, renderable, CPU readable, scanoutable — разные capabilities |

Alignment, padding, tiled addressing, compression metadata нельзя восстановить догадкой. `DRM_FORMAT_MOD_LINEAR` — конкретный layout. `DRM_FORMAT_MOD_INVALID` — sentinel для отсутствия explicit modifier/implicit contract; это **не синоним linear**. [Modifier specification][spec-modifiers], [linux-dmabuf protocol][proto-dmabuf].

### 6.3. Что ломает direct path

Импорт невозможен или бесполезен при любом из условий:

- FD не dma-buf, driver не поддерживает импорт этого exporter или не умеет attach/map memory.
- Нет поддерживаемого format/modifier combination, plane count или pitch/alignment.
- Memory layout доступен GL texture engine, но недоступен display engine.
- Нужны scaling, rotation, blending, color conversion или cursor composition, не поддерживаемые plane.
- Не установлена безопасная synchronization между producer и display engine.
- Не хватает display bandwidth, CRTC/plane resources либо modeset authority.
- Размер/usage allocation не соответствует ограничениям device.

Для NVIDIA 470 наличие PRIME в README не отвечает на вопрос об arbitrary foreign dma-buf → scanout. Обязательный аппаратный microtest должен включать **export → import → FB → TEST_ONLY → display patterned image**, а не заканчиваться успешным `drmPrimeFDToHandle()`.

У buffers, спрятанных в EGLStream, может вообще не существовать доступного приложению dma-buf exporter. В таком случае цепочку нельзя даже начать. Исторический wlroots-eglstreams allocator прямо возвращает false из своей функции получения dma-buf для stream buffers. [Historical allocator][wlr-allocator].

### 6.4. Если modifier не поддержан

Порядок поиска пути задаётся возможностями, а не wishful fallback:

1. Попытаться direct scanout, только если весь scanout contract подтверждён.
2. Если EGL importer поддерживает source layout — sample source и render в поддерживаемый output target.
3. Если source можно копировать GPU-side в другой renderable/exportable allocation — выполнить copy и повторно проверить destination contract.
4. CPU fallback возможен только при корректной CPU mapping/readback и synchronization. `mmap()` tiled/compressed dma-buf не даёт автоматически linear pixels.
5. Если источник невозможно прочитать, отказать с понятной причиной.

«Render-to-linear» требует реально renderable linear destination. Установка modifier=LINEAR на existing tiled storage — повреждение интерпретации, не конверсия. Пересоздание destination allocation меняет transport semantics и счётчик copies.

### 6.5. Нужен ли GBM N-Legacy

Wayland задаёт objects/messages; dma-buf — kernel sharing; DRM/KMS — scanout/modesetting; EGL — contexts/images/surfaces; EGLStream — producer/consumer transport; GBM — allocator API.

**N-Legacy с NVIDIA EGLStream output может не использовать GBM вообще.** Это не освобождает его клиента, Aquamarine, от собственного GBM path. Добавлять GBM к N-Legacy следует только для проверенного отдельного DRM/GBM backend либо diagnostic producer, не «потому что это Wayland».

## PART 7 — Synchronization

### 7.1. Необходимы четыре разных события

Для каждого submission различать:

```text
producer write complete
    → consumer may read
consumer read complete
    → producer may reuse input
output rendering complete
    → display may scan output
scanout replaced/disabled
    → output allocation may be reused/freed
```

При composition входной client buffer может освободиться **раньше**, чем output попадёт на монитор. При direct scanout input одновременно является scanout allocation и удерживается дольше. Ни `eglSwapBuffers()` return, ни `wl_surface.commit`, ни frame callback не являются универсальной заменой этим dependency edges.

### 7.2. Primitive matrix

| Primitive | Гарантия | Чего не гарантирует |
|---|---|---|
| dma-buf implicit sync | Driver contracts используют reservation fences shared allocation | Сам FD не доказывает, что конкретный driver публикует/ждёт все нужные fences |
| `sync_file` FD | Переносимый Linux fence container, ожидаемый соответствующими APIs | Не buffer FD, не timeline syncobj FD |
| `EGLSync` fence | Зависимость от команд конкретной EGL/GL execution domain | Экспорт в FD без native-fence extension не следует |
| `GLsync` | Fence/sync в GL API | Handle не передаётся другому process как универсальный Linux fence |
| DRM binary syncobj | Kernel sync object, пригодный для поддерживаемых ioctls | Наличие binary не доказывает timeline support |
| DRM timeline syncobj | Зависимости по monotonically addressed timeline points | Не реализуется объявлением Wayland global без driver support |
| Atomic `IN_FENCE_FD` | Plane не должен читать buffer до acquire fence | Эта property должна существовать и работать у driver |
| Atomic `OUT_FENCE_PTR` | Completion fence commit по KMS semantics | Не означает, что newly displayed buffer уже перестал сканироваться |
| EGLStream acquire/release | Ownership и ordered GL consumer access по stream contract | Не общий dma-buf/CPU fence adapter |
| `wl_buffer.release` | Compositor больше не использует эту buffer usage по protocol contract | Не время физического показа; нельзя посылать до окончания read |
| `wl_surface.frame` callback | Подсказка, когда клиенту готовить следующий frame | Не release fence и не точный presentation timestamp |
| `wp_presentation` | Presented/discarded feedback и presentation clock | Не предоставляет ownership или разрешение reuse |

Основания: [kernel dma-buf][kernel-dmabuf], [DRM sync objects][drm-uapi], [Wayland core][wayland-core], [presentation-time][proto-presentation], [EGL native fence][spec-native-fence].

### 7.3. Два Wayland explicit-sync протокола

Старый `zwp_linux_explicit_synchronization_v1` передаёт acquire fence FD и buffer release information. Современный `wp_linux_drm_syncobj_manager_v1` использует DRM timeline syncobj и acquire/release points. Это не один protocol с разными именами. Resource lifetime и handling commit без buffer надо реализовывать согласно именно выбранному XML. [Explicit synchronization][proto-explicit], [DRM syncobj protocol][proto-syncobj].

Нельзя рекламировать syncobj protocol на 470, основываясь на том, что header современного ядра содержит syncobj ioctl. Нужны capability query, import/export tests, signal/wait и доказанная GPU integration.

У проверенного nested Wayland backend Aquamarine нет полноценного forwarding этих parent-side syncobj dependencies. Поэтому объявление такого global внешним compositor не доказывает его использование Hyprland output. Проверять wire trace конкретной версии.

### 7.4. Можно ли жить на implicit sync

**Да, если все участники конкретного маршрута соблюдают совместимый contract.** Исторические desktop stacks так работали. Но нельзя переносить корректность NVIDIA EGLStream internal synchronization на arbitrary exported dma-buf. Для каждой комбинации exporter/importer/usage нужен тест «producer пишет медленно, consumer читает немедленно, producer переиспользует aggressively».

Без explicit FD можно использовать documented stream acquire/release. Для diagnostic copy path можно временно использовать blocking waits **в producer execution domain**, после чего передавать completed contents. `glFinish()` в N-Legacy не ждёт незнакомую очередь Hyprland. Сервер не может исправить произвольное нарушение producer sync одним локальным `glFinish()`.

`DMA_BUF_IOCTL_SYNC` относится к CPU access/cache coherency contract. Оно не подменяет произвольную межпроцессную GPU synchronization схему. При mmap read/write нужны правильные START/END направления и поддержка exporter; после CPU upload reuse source безопасен лишь после потребления CPU bytes или соответствующего upload lifetime.

### 7.5. Типовые гонки

| Ошибка | Последствие | Исправление |
|---|---|---|
| Release input сразу после GL draw submit | Producer перезапишет source до texture read | GPU completion dependency либо stream release semantics |
| Reuse output после события его первого показа | Display ещё читает этот же buffer | Дождаться replacement/disable completion |
| Frame callback сразу на каждый commit | Producer flooding, growing queue | Bounded scheduler с feedback из presentation/readiness |
| FIFO acquire в Wayland event thread блокирует | Client ждёт protocol reply, server ждёт client frame | Неблокирующая проверка/отдельный graphics worker и bounded handoff |
| Sync FD путается с buffer FD | Invalid imports или неверные waits | Разные типы wrappers и constructors |
| Destroy stream, потом release тем же handle | Invalid handle / утечка acquired image | Release до destroy; аварийный cleanup согласно consumer spec |
| Один buffer показан на двух outputs, release после одного | UAF/overwrite на втором | Usage refs по каждому consumer/output |
| Fence wait не flush’ит producer work | Взаимное ожидание или вечный timeout | Корректный flush/submit в owner context, watchdog только для диагностики |

### 7.6. LEVEL 2: можно ли эмулировать semantics

| Предложенное соответствие | Оценка | Что потребуется дополнительно |
|---|---|---|
| FIFO → userspace queue | **Partial, emulatable** | Bounded capacity, producer blocking, frame order, cancellation, acquire semantics |
| Mailbox → latest slot | **Partial, emulatable** | Нельзя перезаписать acquired slot; displaced pending frame надо освободить корректно |
| Acquire → mutex/refcount | **Не exact** | Refcount сохраняет lifetime, но не завершает producer GPU writes |
| Release → decrement refcount | **Не exact** | Нужны GPU read completion и право producer reuse |
| Timing → vblank/pageflip | **Partial** | Timestamp domain, skipped frames, presentation vs readiness, output-specific clocks |
| Ownership → explicit state | **Necessary, insufficient** | State changes должны подкрепляться реальными fence/driver guarantees |
| EGL fence → sync_file | **Conditional** | Только через поддерживаемые export/import APIs |
| EGL fence → eventfd | **Not equivalent** | CPU notification не становится kernel GPU dependency без отдельного wait/bridge |
| EGLStream image → dma-buf | **Не semantic mapping** | Нужен доступ к memory allocation или реальное копирование |

Эмулировать очередь полезно. Эмулировать отсутствие allocator/export API описанием очереди невозможно.

## PART 8 — Historical implementations

### 8.1. KWin: наиболее полезный прочитанный reference

Проверенные файлы v5.22.0:

- `src/plugins/platforms/drm/egl_stream_backend.cpp`: EGLDevice matching, master FD, stream producer/output consumer, external texture input, resize/recreation, acquire/flip event.
- `src/plugins/platforms/drm/drm_output.cpp`: atomic/legacy modeset logic, dumb buffers, distinction between EGLStream flip notification and ordinary DRM pageflip.

Почему работало: **KWin сам имел EGLStream-aware renderer/output backend и client stream importer**. Это не внешний bridge, чудесным образом меняющий непонимающий EGLStream compositor. Переносимы partition per output, matching EGL/DRM devices, generation changes и ordered presentation. Устарели KWin/KWayland internal APIs, integration assumptions и конкретные workaround’ы старых драйверов. [KWin EGL][kwin-egl], [DRM][kwin-drm].

### 8.2. wlroots-eglstreams: полезен и как контрпример

Проверены:

- `render/egl.c` — EGLStream/EGLDevice integration;
- `render/eglstreams_allocator.c` — stream-oriented allocator и references на per-plane stream;
- `backend/drm/renderer.c` — отличие stream buffers от обычных dma-buf FB;
- `protocol/wayland-eglstream-controller.xml` — controller protocol;
- README и LICENSE.

README сообщает, что DMA-BUF в основном работает, но `buffer_get_dmabuf()` именно **stream allocator buffer** возвращает false. Это не обязательно противоречие: read/import support для клиентских buffers и export output allocation — разные возможности. Делать из общей фразы README вывод «EGLStream легко экспортируется в dma-buf» нельзя. [README][wlr-readme], [allocator][wlr-allocator], [DRM renderer][wlr-drm-renderer].

Этот fork менял allocator, renderer и DRM integration. Перенос его в новый N-Legacy не добавит соответствующие изменения stock Aquamarine. Использовать fork как бинарную production dependency означает поддерживать ещё один исторический graphics stack и его взаимодействие с текущим kernel/userspace.

### 8.3. Weston

Официальный README NVIDIA 470 ссылается на EGLStream-enabled Weston в архиве James Jones `~jjones/weston`. Это подтверждает существование reference path. Однако конкретный архивный patchset не удалось надёжно прочитать в этой сессии. Поэтому **не выдаю придуманные commit IDs и exact filenames этого fork за проверенные**. [NVIDIA reference][nv-components], [архив Weston][weston-history].

Для современной архитектуры Weston полезен как reference compositor lifecycle, DRM output state, libweston и GL renderer. Но наличие современного `libweston` не доказывает сохранение или ABI-совместимость старого EGLStream backend. Исторические DRM/GL изменения надо извлечь из конкретного commit, проверить LICENSE каждого файла и только затем оценивать перенос. Это остаётся отдельной source-audit задачей, а не блокирует выявленный Aquamarine barrier.

### 8.4. NVIDIA egl-wayland и EGLStream/KMS samples

Самый полезный NVIDIA reference здесь — не небольшой «triangle sample», а реально прочитанные `wayland-eglsurface.c`, `wayland-eglswap.c`, `wayland-egldisplay.c`, `wayland-eglstream-server.c` и protocol XML. В них видны handshake, damage thread, resource release и resize.

EGLDevice/KMS samples полезны для первого физического кадра. Их использование должно начинаться с проверки device selection и доступных extensions; успешный demo producer не подтверждает, что Hyprland умеет производить такой же stream. Не требуется тащить весь SDK или CUDA в N-Legacy.

### 8.5. License / reimplementation

| Источник | Обнаруженные/типичные условия | Практическое решение |
|---|---|---|
| wlroots / просмотренный fork | MIT-style LICENSE; проверить новые файлы отдельно | Можно reuse с сохранением notices; отсутствие ABI stability не лицензионная проблема |
| Weston | MIT-style COPYING, отдельные third-party notices | Можно reuse после проверки provenance конкретного patchset |
| KWin v5.22 EGLStream files | SPDX `GPL-2.0-or-later` | Прямой перенос требует выполнения GPL obligations для resulting derivative; для permissive проекта предпочесть самостоятельную реализацию по specs |
| NVIDIA egl-wayland | MIT-style permissions; в repo есть отдельные third-party notices | Подходит для reuse с attribution; закрытый driver не становится открытым |
| Mesa | В основном permissive, но лицензии проверяются по конкретным файлам | Не считать весь repository одним безусловным MIT blob |
| Linux kernel | File-level SPDX; UAPI headers могут иметь syscall exception | Не копировать implementation kernel в proprietary/permissive userspace без анализа лицензии |
| Protocol XML / Khronos specs | Собственные copyright/license тексты | Генерировать bindings/реализовывать APIs с сохранением нужных notices |

Это инженерная оценка по LICENSE/SPDX, а не обещание юридической совместимости любого сборного продукта. [wlroots LICENSE][wlr-license], [KWin source header][kwin-egl], [Weston COPYING][weston-license], [Mesa licensing][mesa-license], [egl-wayland README][egl-readme].

«Изучил GPL-код и написал самостоятельно» нельзя автоматически называть формально clean-room. Для строгой clean-room процедуры спецификатор и implementer разделены, фиксируются доступные сведения и provenance. Для обычной независимой реализации опубликованного API clean-room не является безусловным требованием; важно не переносить защищённый код без соответствующих обязательств.

## PART 9 — Architecture alternatives

Ни одна оценка ниже не означает измеренную производительность. «Дополнительный draw» — работа после уже готового кадра Hyprland; рендеринг самого Hyprland не включён в этот счётчик.

### 9.1. Feasibility, APIs, copies и задержка

| № | Архитектура | Feasibility при исходных ограничениях | APIs и copy/performance profile |
|---|---|---|---|
| 1 | Outer Wayland + stock nested Hyprland + EGLStream output | Output реален; producer side не доказан, native 470 GBM отсутствует | linux-dmabuf input → EGL import → GL draw → stream output; обычно минимум один дополнительный draw, если вход существует |
| 2 | Outer Wayland + nested Hyprland + dma-buf → KMS | Не обходит allocator barrier; direct import 470 отдельно UNKNOWN | PRIME/AddFB/atomic; потенциально true zero-copy на участке bridge при полном совпадении contracts |
| 3 | EGLStream-native frontend → GL → EGLOutput | Обоснован для EGLStream-клиента; stock Hyprland таким output-клиентом не является | Wayland EGLStream + external texture + второй stream; zero CPU copy, дополнительный GL pass |
| 4 | EGLStream → GPU draw → иной output target | Conditional: target allocation/renderability должны существовать | External sampler → FBO; один draw, затем destination presentation; нового allocator не создаёт |
| 5 | EGLStream → EGLImage → dma-buf export → DRM | Не доказан; универсальной такой конверсии нет | Нужны конкретный image-consumer/export API и scanout compatibility; нельзя обещать zero-copy |
| 6 | Mesa GBM allocation + NVIDIA EGL import | PLAUSIBLE только для конкретной комбинации, не NVIDIA native GBM | GBM/export + EGL import; texture-only импорт ещё не обеспечивает Hyprland render target |
| 7 | Старый wlroots-eglstreams как outer host | Может подтвердить legacy output, но не исправляет stock producer | Исторический EGLStream allocator/renderer; copy profile как №3 или №1 |
| 8 | Старый KWin/Weston EGLStream host | Reference/diagnostic; текущая сборка не гарантирована | EGLDevice/EGLOutput; output зрелее маленького demo, но современный client contract остаётся |
| 9 | Новый custom compositor на современном wlroots | Потребуются собственные stream backend/renderer adapters | Уменьшает protocol work, увеличивает интеграцию с evolving wlroots APIs |
| 10 | Custom libwayland-server compositor | Лучший узкий экспериментальный host, если P0 пройден | Только необходимые protocols, GL, libdrm, EGL; copy profile зависит от input importer |
| 11 | Встроить существующий compositor/backend как library | libweston осмысленнее embedding KWin; старый backend всё равно портировать | Цена dependency graph и versioned API; не транспортное решение само по себе |
| 12 | Xorg/i3 → Wayland host в X11 window → nested Hyprland | Отличный output diagnostic; Hyprland GBM barrier остаётся | Host рисует через NVIDIA GLX/EGL X11; лишний window presentation layer, input/focus nesting |
| 13 | Xwayland как «обратный bridge» | FALSE ASSUMPTION | Xwayland — X server/Wayland client, он не делает Wayland compositor X11-клиентом |
| 14 | Vulkan display/WSI или Zink | Не обходят stock Aquamarine автоматически | Нужны нужные Vulkan extensions, external memory/sync и совместимый GL-on-Vulkan stack; driver 470 capabilities тестировать |
| 15 | Mesa software rendering + настоящий DRM/GBM exporter → CPU upload → NVIDIA output | PLAUSIBLE эксперимент при неизменном GPU/470, **не готовый рецепт** | Возможно `vkms`/kms_swrast или другой настоящий allocation path; software cost + CPU transfer, производительность UNKNOWN |
| 16 | Headless Hyprland → capture/PipeWire → output client | Не исправляет renderer init; conditional после работающего producer | Capture/readback/possibly encoding увеличивают copies и latency; input injection отдельная задача |
| 17 | Nouveau/Mesa на GTX 650 | Самая прямая software-stack альтернатива, но нарушает условие 470 | Обычные GBM/dma-buf/KMS; производительность/reclocking/стабильность проверять на этой карте |
| 18 | Совместимый дополнительный/новый GPU | Наиболее предсказуемый путь к modern stack, но меняет hardware scope | Direct modern Hyprland; при выводе через старый GPU снова нужны cross-device sharing/copy tests |

### 9.2. Operational complexity

Обозначения: низкая/средняя/высокая — относительная инженерная сложность, не число человеко-месяцев. `M/R` — multi-monitor/resize, `C/S` — crash recovery/suspend. Условие «решён вход» во всех nested вариантах обязательно.

| № | Sync / lifetime | Maintenance / dependencies | M/R | C/S | Зависимость от NVIDIA-specific поведения |
|---|---|---|---|---|---|
| 1 | Высокая: два graphics domains | Средняя custom host, высокий legacy risk | Возможны; nested restrictions | Session restart, recreate streams | Высокая output, плюс неизвестный import |
| 2 | Средняя при доказанных fences; scanout refs сложнее | Средняя | KMS constraints, пересоздание FB | KMS/session rebuild | Высокая на 470 import/scanout |
| 3 | Средняя внутри stream contract; два разных stream lifetimes | Средняя custom host | Per-output streams; resize generations | Stream/context recreation | Высокая, но reference paths существуют |
| 4 | Высокая на границе source/destination | Высокая при нескольких allocators | Зависит от destination | Зависит от обоих stacks | Высокая |
| 5 | Очень высокая, feasibility не доказана | Высокая | UNKNOWN | UNKNOWN | Критическая |
| 6 | Высокая cross-implementation sync | Высокая version sensitivity | После успешного usage test | Между Mesa/NVIDIA domains | Критическая совместимость импорта |
| 7 | Исторически реализована, заново проверять | Очень высокая поддержка старого fork | Заявлены в README | Заявлены частично, не гарантия сегодня | Высокая |
| 8 | Исторически реализована | Очень высокие desktop dependencies | Есть reference algorithms | Историческая поддержка | Высокая |
| 9 | Высокая adapter work | Высокая wlroots API tracking | Helpers полезны | Helpers полезны, driver reset остаётся | Высокая output |
| 10 | Собственная ответственность, ограничивается scope | Низкие базовые dependencies, средняя/высокая собственная работа | Делать по этапам | Явные state machines | Локализована в одном domain |
| 11 | Library + legacy backend lifecycle | Средняя/высокая, versioned library | Возможны | Lifecycle не всегда доступен через public API | Зависит от backend |
| 12 | X11 present + Wayland pacing | Средняя, Xorg нужен постоянно | Window mapping проще, physical policy у Xorg | Xorg session dependency | NVIDIA X11 путь зрелее, bridge вход не решён |
| 13 | Неприменимо | Неприменимо | Не решает задачу | Не решает задачу | Не архитектура нужного направления |
| 14 | Высокая external memory/sync complexity | Высокая для ненужного нового API layer | WSI-specific | Driver-specific | Очень высокая на 470 |
| 15 | CPU path проще объяснить; exporter coherence проверить | Средняя + software stack constraints | Технически возможно, performance limit | Device/worker restart | Ниже на readback; output остаётся legacy |
| 16 | Capture scheduling + buffer release + injection | Высокая dependency complexity | Capture sessions per output | Capture reconnect + compositor restart | Вход всё равно остаётся |
| 17 | Стандартные Mesa/KMS contracts | Ниже custom legacy проекта | Обычный compositor path | Hardware tests необходимы | Proprietary NVIDIA зависимости нет |
| 18 | Низкая при прямом modern output | Обычно самая низкая | Обычный compositor path | Обычный поддерживаемый stack | Нет на прямом пути; есть при legacy output |

### 9.3. Почему software fallback нельзя обещать одной переменной

`LIBGL_ALWAYS_SOFTWARE=1` влияет на Mesa, а не превращает vendor NVIDIA EGL в software EGL. Должны одновременно работать выбор Mesa vendor, настоящий GBM allocation, export всех planes, import как Hyprland render target и доставка linux-dmabuf.

Прочитанный Mesa `gbm_dri.c` содержит software/kms_swrast и dumb paths, но также ограничения fd export для разных BO representations. Поэтому ни `gbm_create_device()` success, ни картинка обычного llvmpipe window demo не доказывают нужную цепочку. [Mesa GBM implementation][mesa-gbm].

Виртуальный KMS device полезен только при наличии пригодного producer path. Fake `main_device` в feedback или выдуманный render node нарушают контракт и не являются решением. Тест реального `vkms`/Mesa exporter — допустимый эксперимент; глобальная fake GBM layer исключена исходными требованиями.

## PART 10 — Recommended architecture

### 10.1. Правильная абстракция

Если N-Legacy владеет physical output и принимает Wayland surfaces, он является **outer Wayland compositor с ограниченной shell policy и NVIDIA presentation backend**. Названия display bridge / nested host описывают назначение, но не отменяют обязанностей compositor.

Он обязан владеть surface pending/current state, shell configure lifecycle, buffer usage, input focus, damage, callbacks и output lifecycle. Не обязан реализовывать полноценный desktop/window manager: можно разрешить одному доверенному child compositor по одному fullscreen toplevel на output.

`EGLStream-to-dmabuf translation layer` — неправильное название рекомендуемой базы: такая конверсия не доказана и не нужна для stream-native output.

### 10.2. Архитектура с явным gate

```text
                         P0: stock producer gate
                         ┌───────────────────────┐
                         │ stock Hyprland        │
                         │ working allocator     │
                         │ working EGL renderer  │
                         └───────────┬───────────┘
                                     │ real linux-dmabuf + valid sync
                                     ▼
                ┌──────────────────────────────────┐
                │ N-Legacy                         │
                │ Wayland shell / surfaces         │
                │ input import capability check    │
                │ per-submission ownership         │
                │ GL composition OR CPU upload     │
                │ NVIDIA stream producer per output│
                │ EGLOutput / DRM session          │
                └────────────────┬─────────────────┘
                                 ▼
                           GTX 650 display
```

Параллельно, без Hyprland, тестируются SHM и NVIDIA EGLStream clients. Их успех подтверждает соответствующий importer/output path, но не заменяет P0.

### 10.3. Go / no-go

Продолжать реализацию Hyprland host только если P0 показывает:

- неизменённые binaries Hyprland/Aquamarine;
- реально выбранный и пригодный DRM/GBM allocator;
- реальный rendered output buffer с корректными metadata;
- возможность N-Legacy прочитать его с корректной synchronization;
- приемлемую стоимость producer rendering и transfer;
- отдельную перспективу запуска нужных приложений внутри Hyprland.

Если P0 провален, native EGLStream backend можно сохранить как самостоятельную работу, но проект «совместимость Hyprland без изменений» считается **заблокированным по архитектуре**, а не «почти готовым, осталось подключить client».

### 10.4. wlroots vs custom

Для малого hardware proof выбрать **libwayland-server + libdrm + EGL/GLES**, без wlroots. Это уменьшает объём version-sensitive integration и позволяет явно выразить stream resources без притворства GBM BO.

Для более широкого production compositor wlroots даёт ценные protocol/input helpers, но потребует совместимых custom renderer/allocator/output interfaces и сопровождения API. libweston может дать больше готового lifecycle, однако портирование legacy backend и зависимость от versioned ABI остаются. Встраивать KWin ради нескольких EGLStream functions нецелесообразно.

Это не тезис «написать Wayland compositor легко». Узкая one-child policy лишь ограничивает scope; protocol correctness, security и recovery всё равно обязательны.

## PART 11 — First-frame prototype

### 11.1. Три разные milestones

| Milestone | Что доказывает | Чего не доказывает |
|---|---|---|
| P0: Hyprland frame создан и доступен | Producer allocator/render/export path | Физический NVIDIA output |
| P1: собственный pattern через EGLOutput на монитор | Native NVIDIA presentation | Совместимость Hyprland |
| P2: identifiable Hyprland frame физически показан | End-to-end конкретного выбранного пути | Ускорение приложений, production stability, zero-copy |

В P2 идентифицировать кадр изображением с уникальным счётчиком/паттерном в Hyprland и журналом соответствующего commit. Просто цветной экран или лог «buffer imported» не достаточны.

### 11.2. Минимальный process model

Один `n-legacy` process, один event loop, один GL-owning thread, один output, один client. Hardware probes — отдельные executables, чтобы driver crash не разрушал большой session manager. Shell/seat lifecycle и input registration можно начать с пустого seat, если клиент требует сам global.

Wayland/DRM/seat FDs подключены к event loop. Graphics operations не должны делать бесконечный blocking wait. Если конкретный EGL operation способен надолго блокироваться, следующим этапом выделяется graphics worker с bounded job queue; сам compositor protocol state остаётся на owner thread.

### 11.3. Строгий initialization sequence для P1/native output

1. Прочитать config, limits и выбранный device; создать logging и cleanup ledger.
2. Открыть session через libseat/logind backend либо выбранный seatd backend; дождаться active state. Не запускать desktop постоянно root.
3. Получить primary DRM FD через session mechanism; сопоставить PCI device и driver. Render node не предоставляет modeset authority.
4. Query KMS resources, universal planes/atomic support и properties. Выбрать connected connector, compatible CRTC, primary plane и EDID mode.
5. Сохранить доступное исходное состояние для корректного выхода; создать mode blob и при необходимости dumb bootstrap FB.
6. Query EGL client extensions и EGLDevices; найти устройство, соответствующее DRM node через `EGL_DRM_DEVICE_FILE_EXT`/device identity.
7. Создать device EGLDisplay с `EGL_DRM_MASTER_FD_EXT`, вызвать `eglInitialize`; записать vendor/version/extensions. Убедиться, что загружен ожидаемый vendor.
8. Проверить обязательные output/stream extensions и entrypoints. Function pointer сам по себе недостаточен.
9. Выбрать EGLConfig с `EGL_STREAM_BIT_KHR`, нужным GL/GLES renderable type и цветовой глубиной; создать context.
10. Создать output stream; выбрать EGLOutput layer по выбранному plane/CRTC; подключить consumer **до producer**.
11. Создать `eglCreateStreamProducerSurfaceKHR` с width/height mode; make current и настроить viewport.
12. Выполнить согласованный bootstrap modeset. При manual-acquire пути ориентироваться на KWin: dumb FB служит modeset state, stream — rendered content. Порядок modeset/acquire проверить на 470; не вести один plane двумя независимыми presentation controllers.
13. Нарисовать pattern и swap. Для simple auto-acquire demo дождаться видимого результата; для manual path выполнить предусмотренный acquire и обслужить DRM event.
14. Подтвердить физический показ. Записать mode, frame sequence и полученное событие. При отсутствии достоверного presentation event не выдумывать timestamp.

Пункты 1–11 определены API dependencies. Пункт 12 является **driver integration point**, а не универсальной последовательностью всех EGL implementations. [KWin][kwin-egl], [KMS][nv-kms], [EGL/DRM mapping][spec-device-drm].

### 11.4. После P1: server initialization

1. Создать `wl_display`, event sources, compositor/surface implementation и shell policy.
2. Создать `wl_shm`; реализовать `wl_seat` требуемой версии с честными capabilities.
3. Реализовать `xdg_wm_base`, initial empty commit → configure → ack → first buffer attach.
4. Для NVIDIA stream client сделать EGL-Wayland bind и controller, проверить handshake тестовым EGL-клиентом.
5. Для Hyprland объявлять linux-dmabuf v4 только при реально работоспособном importer и честном feedback: valid main device, format table, tranches.
6. Создать socket с уникальным именем, например `n-legacy-0`; сигнализировать readiness после готовности обязательных globals и output.
7. Запустить child с parent `WAYLAND_DISPLAY=n-legacy-0` и отдельным минимальным config, подходящим к **его версии** Hyprland.
8. Проверить allocator/render/backend logs, дождаться configure/attach/commit; перейти к P2 только если producer gate выполнен.

Не надо публиковать фиктивный dmabuf global для обхода startup check, если за ним нет возможности принимать advertised buffers.

### 11.5. Exact first Hyprland frame flow — только при пройденном P0

1. Parent N-Legacy отправляет xdg configure с output size; Aquamarine подтверждает serial.
2. Aquamarine создаёт/reconfigures GBM swapchain с совместимым format/modifier.
3. Hyprland выбирает свободный BO, создаёт/использует EGL/GL render-target view и рисует desktop frame.
4. Producer отправляет GPU work; корректность дальнейшего чтения обеспечивается конкретным проверенным sync contract. Wayland commit сам fence не создаёт.
5. Aquamarine экспортирует plane FDs/metadata. `zwp_linux_buffer_params_v1.add` передаёт FD references серверу; `create_immed` создаёт `wl_buffer`.
6. Aquamarine делает `wl_surface.attach`, `damage_buffer`, `frame`, `commit`, затем flush.
7. N-Legacy переводит pending surface state в committed submission и держит reference независимо от жизни protocol object.
8. N-Legacy проверяет acquire dependency. GPU-import route создаёт/переиспользует EGLImage + texture; CPU route читает только поддерживаемый completed source representation.
9. GL context N-Legacy рисует input texture в **другой** allocation, принадлежащий output stream producer. При CPU route перед этим происходит upload.
10. После завершения потребления input его usage становится releasable. Для обычного dma-buf `wl_buffer.release` отправляется по корректному contract, не до завершения read.
11. N-Legacy вызывает `eglSwapBuffers()` output producer surface; stream делает output frame доступным consumer.
12. Auto-acquire либо manual output acquire schedules presentation. Driver выполняет необходимую GPU/display synchronization.
13. Display engine начинает scanout; output event подтверждает соответствующий presentation stage, если поддерживается.
14. N-Legacy отправляет frame callback в выбранном pacing policy и presentation feedback лишь с фактическими данными.
15. Предыдущий output image возвращается stream producer pool согласно driver/consumer lifecycle; N-Legacy не подменяет это ранним ручным release клиентского buffer.

**На native 470 + stock Aquamarine эта последовательность может остановиться уже на шагах 2–3.** Последующие шаги — конкретный проект маршрута, а не заявление о его выполнении.

### 11.6. Shutdown order

Прекратить принимать новые jobs → остановить child launch/restart → снять visible surfaces и отменить pending callbacks корректным disconnect → дождаться/отменить поддерживаемым способом in-flight work → disable/retire outputs → release acquired stream frames → destroy textures/images/import views → destroy producer surfaces → destroy streams → EGL-Wayland unbind и protocol teardown → destroy context/display → удалить свои FB/GEM/mode blobs после retirement → вернуть seat/devices → закрыть FDs.

При GPU hang некоторые waits могут не завершиться. Тогда аварийный process teardown и восстановление session — отдельная ветка; нельзя обещать идеальную очистку каждого GPU resource через зависший driver.

## PART 12 — Production architecture

### 12.1. Минимальное разделение ответственности

- **Session**: device ownership, VT activation, suspend coordination.
- **Guest desktop**: one-child shell policy, mapping nested toplevels to physical outputs, child lifecycle.
- **Surface content**: immutable descriptors и per-commit usage objects.
- **NVIDIA display**: EGLDevice/context, stream input consumer при необходимости, stream output, GL composition.
- **Output scheduling**: bounded queue, damage history, callbacks, presentation bookkeeping.
- **Input seat**: libinput/xkbcommon → Wayland events, focus, emergency actions.

Это логические контракты, а не требование построить plugin framework. Первоначально всё можно статически связать в один executable.

### 12.2. Почему production может быть composition-only

Direct scanout не нужен для корректного desktop. На legacy NVIDIA более разумна одна проверенная composition route, чем частые переключения stream composition ↔ undocumented dmabuf scanout. Цена — дополнительный full-screen draw и bandwidth. Выигрыш — единые rules для scaling, cursor, damage и lifetime.

Поддерживать direct scanout имеет смысл только после profiling, если source storage, fences, plane eligibility и transition tests доказаны. Нельзя называть stream presentation zero-copy всего desktop: перед output уже мог быть composition pass.

### 12.3. Restart boundaries

Если падает Hyprland, outer compositor может оставаться жив, освободить его surface usages и показать diagnostic screen. Перезапустить Hyprland можно, **но обычные приложения, подключённые к погибшему Hyprland, потеряют display connection**; это не transparent desktop recovery.

Если падает N-Legacy, parent connection Hyprland разрывается. Автоматический reconnect всего compositor graph не является свойством Wayland. Базовая policy — перезапуск всей nested session, с сохранением логов и ограничением restart rate.

Watchdog полезен для missing progress и бесконечных restart loops. Он не чинит GPU hang и не гарантирует reset Kepler без reboot. Не делать бесконечные driver reinit cycles на kernel GPU fault.

### 12.4. Совместимость на будущее

Расширять capabilities по операциям (`can_sample`, `can_render`, `can_export`, `can_scanout`, `can_cpu_read`, supported_sync), а не по единственному флагу `is_nvidia_legacy`.

Достаточно трёх точек расширения: **input import**, **composition target**, **presentation**. EGLStream поддерживать отдельным opaque endpoint/frame lease; не заставлять каждый resource иметь GEM/FB/EGLImage fields. GBM/KMS backend можно добавить позже при реальной потребности. Включать Nouveau как «ещё один special case NVIDIA 470» не нужно: это иной driver и нормальный Mesa path.

## PART 13 — Buffer lifetime / state machines

### 13.1. State machine должна принадлежать usage, а не только allocation

Один `wl_buffer` может использоваться повторно и несколькими surfaces. Поэтому единственная переменная `buffer.state = PRESENTING` недостаточна. Разделить **allocation**, **protocol resource**, **committed usage** и **output presentation**.

```text
usage:
COMMITTED → WAIT_ACQUIRE → READABLE → READING
                                      │
                     composition      ├→ READ_COMPLETE → RELEASE_ELIGIBLE
                     direct scanout   └→ SCANOUT → RETIRED → RELEASE_ELIGIBLE
                                                                    │
                                                    protocol release / retire

any pending stage → CANCELLED → retire dependencies → RELEASE_ELIGIBLE
```

`CANCELLED` не означает «прямо сейчас free»: уже начатые GPU reads нельзя отменить сменой enum. Release разрешён, когда **все usages, которые ещё читают storage, завершены** и соблюдены rules выбранного release protocol.

### 13.2. Resource-specific ownership

| Resource | Приобретение | Когда можно освободить локальное представление | Когда можно повторно писать storage |
|---|---|---|---|
| `wl_buffer` resource | Создаёт creator protocol | При destroy client disconnect убрать listener/resource pointer; backend refs живут отдельно | После корректного release всех consumer usages |
| Received dma-buf FD | Server получает собственный descriptor | После successful import, удерживающего kernel ref, если FD больше не нужен для reimport/export/CPU access; failures закрыть | Close не даёт права reuse |
| GEM handle | PRIME/import или allocation на данном DRM file | После окончания userspace use; kernel FB refs и handle refs различны, практично закрывать после retirement views | Только после consumer completion, не после GEM_CLOSE |
| DRM FB | AddFB* | После replacement/disable completion и отсутствия pending commit references | После retirement всех scanout uses |
| GBM BO | Allocator/swapchain | Когда никто не держит read/scanout/import usage; surface front buffers release через соответствующий GBM API | После producer получил свободный swapchain slot |
| EGLImage | Import/create image | EGL/GL sibling rules допускают отдельный lifetime; простой безопасный design держит до retirement texture usage | Не определяется eglDestroyImage |
| GL texture | Context/share group create/import | После прекращения use, с корректным current context; driver отслеживает pending GL commands | Storage ownership всё равно зависит от внешнего producer/consumer contract |
| EGLStream frame lease | Consumer acquire | Release в правильном context; handles stream/frame не уничтожать в неправильном порядке | Управляется stream implementation после release + GPU read completion |
| Output stream image | Producer surface pool | Не вручную как обычный client FB | Stream consumer/producer contract возвращает image в pool |

Для PRIME import одного и того же object в один DRM file возможен один и тот же GEM handle. Нельзя делать GEM_CLOSE по числу plane FDs и случайно закрыть один handle несколько раз. Handle cache должен знать identity и собственные userspace references.

### 13.3. Важная тонкость FB removal

`drmModeRmFB()` для framebuffer, который ещё используется, имеет side effects в KMS lifecycle; это не просто «убрать ID, оставив всё как было». Безопасная policy N-Legacy: сначала успешно заменить/отключить, дождаться completion, затем удалить FB. Новые UAPI вроде close-FB нельзя предполагать у старого driver только по версии libdrm. [DRM UAPI][drm-uapi].

### 13.4. Resize / generations

При resize создать новую output generation: dimensions, EGLSurface, stream и связанные views. Старую generation пометить retiring и освободить после завершения всех её jobs. В callback хранить `{output_id, generation, submission_id}`, а не голый pointer на уже освобождённый output.

Surface current buffer может иметь прежний размер до нового configure/ack/commit. До согласованного перехода выводить старое содержимое по определённой letterbox/crop policy. Не менять geometry старого allocation «на месте».

### 13.5. Frame callbacks и damage

`wl_surface.frame` — double-buffered request: он вступает в силу на следующем commit. Request должен попасть **до** соответствующего commit; рассматриваемый Aquamarine исправляет именно такой порядок. `damage` задаётся в surface coordinates, `damage_buffer` — в buffer coordinates. Scale/transform/viewport должны учитываться при переводе в output damage.

В начале использовать full repaint. Позже учитывать buffer age и accumulated damage; после resize/context loss/output recreation — снова full repaint. Если ещё нет нового content, не вращать busy loop и не порождать неограниченные callbacks. Mailbox scheduler может отбросить superseded pending usages, но должен корректно закрыть их presentation feedback и lifetime.

## PART 14 — Error handling / failure matrix

| Stage | Failure | Наблюдаемый симптом | Diagnostic | Допустимый fallback |
|---|---|---|---|---|
| DKMS/module | Build/load mismatch | Нет NVIDIA devices / module load error | DKMS log, `modinfo`, kernel journal | Известный рабочий kernel+driver package set |
| Session | Нет active seat/master | EPERM, чёрный экран | libseat/logind state, кто владеет device | Перейти на правильную VT; X11-window diagnostic |
| KMS enumerate | Нет connector/CRTC/plane | Output не создан | DRM resource dump, EDID/status | Другой подтверждённый mode/output |
| EGLDevice | Device не сопоставлен | Неправильный vendor/GPU | Node identity + query strings | Исправить selection; не первый device наугад |
| EGL init | Extension/config/context fail | Startup abort | `eglGetError`, vendor/platform logs | Остановиться с точной missing capability |
| GBM producer | Allocation/export fail | Hyprland не выдаёт frame | Aquamarine allocator log и probe | Проверенный другой producer path; не server-side blit |
| Protocol init | Missing globals/versions | Hyprland `Missing protocols` | `WAYLAND_DEBUG=1`, registry dump | Реализовать честный нужный protocol contract |
| Stream handshake | Consumer не подключён | Первый swap зависает/ошибка | Stream state и controller requests | Исправить ordering, bounded failure |
| EGL import | Format/modifier не поддержан | Buffer create fail | Полные plane metadata + EGL error | CPU readable route, только если реально доступна |
| PRIME/AddFB | Import либо FB rejected | Нет direct scanout | errno на каждом этапе, plane IN_FORMATS | GL composition, если source sampleable |
| Render target | Image sampleable, но не FBO-renderable | Incomplete FBO | FBO status, external-only formats | Другой настоящий renderable target |
| Sync | Frame ещё записывается | Мерцание/смешанные кадры | Fence logs, delayed-producer test | Проверенный blocking diagnostic contract либо отказ |
| Atomic commit | EINVAL/EBUSY | Frame не показан | Atomic state dump, TEST_ONLY result | Сохранить previous output; пересобрать state |
| Stream disconnected | Endpoint потерян | Кадры прекращаются | Query stream state/client disconnect | New generation, client/session restart |
| Hot unplug | Connector исчез | Output events прекращены | udev + fresh KMS query | Retire output, перенести desktop policy |
| Context loss | EGL_CONTEXT_LOST | Renderer перестал работать | EGL debug, kernel NVIDIA Xid | Полный graphics-domain reinit или session restart |
| GPU hang | Нет прогресса driver | Зависание нескольких процессов | Kernel journal, NVIDIA report | Ограниченный recovery, затем i3/TTY/reboot при необходимости |
| Invalid client resource | Неверные FD/metadata | Protocol error/import reject | Validation reason с client ID | Отключить виновного клиента, сохранить compositor |

Fallback не должен скрывать смену properties. Логировать `selected_path`, причину downgrade, ожидаемые copies, color/layout changes, sync mechanism и counters. Различать unsupported feature, invalid client input, transient output failure и fatal driver failure.

## PART 15 — Multi-monitor / input / power

### 15.1. Multi-monitor

Per-output state: connector identity, CRTC/plane assignment, mode, transform/scale, output stream/surface, frame scheduler, generation и outstanding presentations. Общий EGLDevice/context допустим, но один blocking swap не должен бесконечно задерживать остальные outputs.

Hotplug algorithm: udev event → fresh resources → построить desired topology → проверить routing/modes → добавить/retire outputs → уведомить nested session policy. Connector IDs могут меняться после device lifecycle; EDID hash полезен, но duplicate/missing EDID не должен ломать identity. Monitor unplug — это не просто `free(output)` при pending callback.

В проверенном Aquamarine host `wl_output` inventory не превращается автоматически в нужное число nested displays. Потребуется session policy, использующая штатные возможности создания/removal nested outputs конкретной версии Hyprland, например поддерживаемый этой версией IPC. Нельзя обещать, что outer `wl_output` hotplug сам выполнит всё. Для первого этапа — один output, integer scale, fixed refresh.

### 15.2. Input

Путь: physical input → libinput → N-Legacy seat/focus → Wayland keyboard/pointer events → Aquamarine → Hyprland → приложения.

Нужны xkbcommon keymap/modifiers/repeat, корректные serials, keyboard focus, pointer enter/leave, buttons/axes/frame grouping и согласованные timestamps. Cursor начать с software composition; hardware cursor добавить после correct plane/lifetime tests.

Отдельно проверить keymap forwarding, layout switching, relative mouse, pointer lock, gestures, tablet, touch и output mapping: ограниченный Aquamarine nested backend может не принимать соответствующие protocols, даже если N-Legacy их реализует. Outer compositor не может заставить клиента использовать неизвестный ему interface.

Emergency VT/exit actions должны принадлежать outer/session layer. Clipboard/DnD и desktop portals не обязательны внешнему one-child host для внутренних приложений, но интеграция с внешней X11 session требует отдельного bridge и не входит в минимальный scope.

### 15.3. VT, lock и power

На deactivate: прекратить новые presentations, остановить physical input delivery, release/revoke device authority по session API, корректно удержать/retire resources. На activate: проверить device identity и connectors, восстановить modes/output generations, full repaint.

Suspend/resume — отдельная система NVIDIA driver power management. README описывает default callbacks и `/proc/driver/nvidia/suspend` integration, включая сохранение video-memory allocations при соответствующих настройках. Не включать все режимы одновременно и не считать новый systemd unit автоматически совместимым с 470. [NVIDIA power management][nv-power].

DPMS/output sleep — backend operation, не остановка Wayland event loop. После wake сбрасывать damage history при сомнении в сохранности contents. Lock screen внутри Hyprland должен быть проверен вместе с outer focus/VT policy: внешний host не должен оставлять видимым старый unlocked frame после disconnect или failed resume.

## PART 16 — Arch development environment

### 16.1. Минимальные пакеты

Названия — Arch package names, доступность и текущие versions проверять перед установкой. Этот документ **не запускал установку** на машине.

| Категория | Пакеты / компоненты | Для чего |
|---|---|---|
| ABSOLUTELY REQUIRED: базовый стенд | Arch base, выбранное `linux-lts` + точно соответствующие `linux-lts-headers`, `dkms` | Один воспроизводимый kernel/driver baseline |
| ABSOLUTELY REQUIRED: 470 | AUR `nvidia-470xx-dkms`, `nvidia-470xx-utils`, их объявленные dependencies | Kernel + совпадающий NVIDIA userspace |
| ABSOLUTELY REQUIRED: сборка | `base-devel`, `pkgconf`, `meson`, `ninja`, `git` | C build, package/revision tracking |
| ABSOLUTELY REQUIRED: N-Legacy runtime | `wayland`, `libdrm`, `libglvnd`, NVIDIA EGL/GLES implementation | Wayland server, KMS, graphics dispatch |
| ABSOLUTELY REQUIRED: безопасный physical session | `libseat` и один рабочий backend: logind **или** seatd | DRM/input device acquisition; не два конкурирующих владельца |
| BUILD для протоколов | `wayland-protocols`; XML EGLStream из совместимого source/package | Генерация bindings; не весь protocol collection нужен runtime |
| REQUIRED для EGLStream clients | `egl-wayland` и корректный external-platform JSON | NVIDIA Wayland integration; версия проходит smoke test |
| REQUIRED для i3 workflow | `xorg-server`, `xorg-xinit`, `i3-wm`, терминал, базовый font package | Постоянная временная рабочая среда |
| REQUIRED только на producer gate | `hyprland`, соответствующий `aquamarine` и dependencies pacman | Проверить именно stock versions |
| OPTIONAL: input stage | `libinput`, `libxkbcommon` | Physical input и keymaps; не нужны для isolated static-color probe |
| OPTIONAL: software/modern reference | Mesa packages, `vulkan-swrast` только при соответствующем эксперименте | Software producer/reference; не добавляют NVIDIA GBM |
| OPTIONAL: X11 apps в Hyprland | `xorg-xwayland` | Отдельный integration stage |
| DEBUG ONLY | `mesa-utils`, `wayland-utils`, `drm_info`, `strace`, `gdb`, `vulkan-tools`, `pciutils`, `jq` | Диагностика capabilities и failures |
| DEBUG ONLY / по доступности | `kmscube`, `apitrace`, perf tooling, sanitizers | Отдельные probes/profiling; kmscube обычно проверяет GBM path, не EGLOutput |
| NOT NEEDED FOR FIRST PHASE | Весь Omarchy, CUDA toolkit, OpenCL, portals, PipeWire, VA-API/VDPAU, 32-bit driver packages, game stack | Не участвуют в доказательстве graphics architecture |

Принципиальная граница: **N-Legacy native backend не обязан линковаться с Mesa/GBM/X11/Xwayland**, однако установленный AUR `nvidia-470xx-utils` сам объявляет dependency на `xorg-server`, `libglvnd`, `egl-wayland`. Поэтому «X11 не нужен архитектурно» не означает «дистрибутивный package set не установит Xorg». [AUR metadata][aur-rpc], [PKGBUILD][aur-pkgbuild].

### 16.2. Kernel и updates

Начать с текущего поддерживаемого **LTS kernel**, для которого используемый AUR patchset собирается и прошёл smoke test на GTX 650. LTS снижает частоту изменений, но не гарантирует совместимость 470. Сохранить второй boot entry с известным рабочим kernel и восстановимую копию package set.

Прочитанный PKGBUILD 470.256.02-8.03 содержит compatibility patches для ряда новых kernel releases и GCC. Это подтверждает продолжающееся community maintenance, **не** hardware validation каждого patch и **не** официальную поддержку NVIDIA новых graphics interfaces.

Особенно чувствительны:

- kernel ↔ matching headers ↔ DKMS patches ↔ compiler;
- загруженный NVIDIA module ↔ `libEGL_nvidia`/GLX/vendor libraries;
- libglvnd loader и vendor JSON;
- egl-wayland ↔ NVIDIA external-platform ABI и compositor protocol behavior;
- Hyprland ↔ Aquamarine ↔ hyprutils и другие version-coupled libraries;
- Mesa ↔ libgbm и software exporter, если выбран экспериментальный Mesa path.

Не смешивать NVIDIA `.run` installation с pacman-owned libraries и не копировать libraries 495 поверх 470. Обновлять согласованный package set, иметь rollback и повторять короткий graphics gate после обновления. Изолированное удержание одной старой shared library в rolling-release системе способно создать unsupported partial-upgrade state.

### 16.3. Правильный workflow с i3

**Режим разработки:** i3/Xorg активен; сборка, unit tests и N-Legacy с X11-window test output. Это проверяет surface/input/import logic, но не physical EGLOutput backend.

**Режим hardware tests:** перейти на отдельную VT/logind session, дать Xorg деактивироваться и освободить device ownership, запустить N-Legacy как active session compositor. i3 может оставаться запущенным на другой VT, но не должен одновременно владеть теми же physical outputs.

**Режим удалённой отладки:** terminal/SSH с другой машины наблюдает logs, пока N-Legacy владеет active VT. Наличие одного GPU не позволяет обещать параллельные независимые Xorg и EGL/KMS masters. DRM lease — отдельная driver capability и topology, а не базовый обход.

### 16.4. Launcher contract

Имена ниже — **предлагаемый CLI N-Legacy**, а не существующие команды из готового проекта:

```sh
# Из active test session после capability gate:
n-legacy --socket n-legacy-0 --device /dev/dri/by-path/REAL_DEVICE \
  --output-policy single --run-guest
```

Launcher обязан: дождаться готовности output/globals/socket → очистить чужие `WAYLAND_SOCKET` и stale instance variables → задать child `WAYLAND_DISPLAY=n-legacy-0` → запустить stock Hyprland с отдельным config → проверить его backend logs. Родительское окружение i3 не менять глобально.

Hyprland создаёт **свой** Wayland socket для приложений и меняет их environment соответственно. Нельзя запускать приложения на внешнем `n-legacy-0`, ожидая увидеть их окна внутри Hyprland. Не экспортировать child-specific software renderer overrides всему systemd user manager.

## PART 17 — Omarchy integration

Прочитанный `install/config/hardware/nvidia.sh` ветки dev выбирает modern NVIDIA packages для Turing+ и ветку 580 для Maxwell/Pascal/Volta. Для неподдержанного GPU выводит сообщение об отсутствии совместимого драйвера. **470 route там не реализован.** Это наблюдение о данном source snapshot, не о любой версии Omarchy. [Omarchy NVIDIA setup][omarchy-nvidia].

Интеграция без форка Hyprland/Aquamarine **организационно возможна**: отдельный package и session launcher. Но она не устраняет graphics barriers из PART 3.

Предлагаемая схема после прохождения tests:

```text
PCI + loaded driver + capabilities detection
  → выбрать modern / tested legacy / unsupported
  → установить согласованный driver package set
  → omarchy-n-legacy session package
  → N-Legacy получает physical seat/output
  → readiness + verified producer capability gate
  → stock Hyprland child
  → приложения/portals подключаются к child display
```

Package `omarchy-n-legacy` должен содержать session entry, launcher, tested configuration profile, diagnostic command и documented rollback to i3/TTY. Base `n-legacy` не зависит от Omarchy.

Дополнительная проверка интеграции: output names вида `WAYLAND-*` вместо `DP-*`/`HDMI-*`, monitor scripts, scaling, DPMS, lock, screenshot/screencast, portals, screen sharing picker и child systemd environment. Physical monitor policy должен выполнять outer layer; обычный Hyprland command управления физическим DRM не обязан работать через nested output.

Detection policy:

1. PCI vendor/device/subsystem IDs через sysfs/libudev, а не `grep NVIDIA`.
2. Kernel driver bound к этому device и module version.
3. DRM primary/render node identity и actual KMS resources.
4. EGL vendor/device/display extensions и successful usage tests.
5. Capability profile с exact package revisions.

Не ставить «legacy supported» по одному условию `driver_version < 495`. Это лишь candidate branch; разные старые поколения и версии драйвера не обязаны иметь одинаковый EGLStream contract.

## PART 18 — Security

Wayland socket предоставляет доступ к compositor import APIs и косвенно к kernel/driver attack surface. Даже one-child policy не отменяет проверку полученных данных.

Обязательные проверки:

- Positive dimensions, maximum texture/output size, project memory budgets и overflow-safe arithmetic.
- Plane indices, отсутствие дубликатов/пропусков, допустимое число planes, modifier consistency по protocol.
- Проверка размера format table и индексов feedback tranches; корректный `dev_t` в main device.
- Для SHM — bounds `offset/stride/height`, минимальный stride, format и доступность mapping. Использовать `wl_shm_buffer_begin_access/end_access` для предусмотренной libwayland обработки SIGBUS, а не беззащитное чтение произвольного client mapping.
- Для dma-buf — type-aware import, разрешённые format/modifier pairs, FD count и resource budgets. `fstat` size не универсальный валидатор tiled/compressed layout; неизвестный layout должен проверяться importer/driver, а не выдуманной формулой.
- Для controller attributes — размер кратен размеру пары, alignment-safe parsing, допустимые keys/values, bounded FIFO length. Не blindly копировать старые pointer casts.
- FD ownership с `CLOEXEC`, closure на каждой error branch, отсутствие double-close, ограничение outstanding resources/commits.
- Bounded queues, backpressure, лимит неотвечающих configure/ping/callback objects. Client flooding не должен неограниченно расходовать RAM/VRAM.
- Не доверять `xdg_toplevel.app_id` как аутентификации Hyprland. Использовать launcher/private socket policy и credentials; same-UID isolation требует отдельной модели угроз.

Для первой версии не включать EGLStream inet transport: local FD transport достаточно. Protocol error клиента не должен вызывать `assert()` всего compositor. Valid client data, которое driver не поддерживает, следует отличать от malformed request; у `create` и `create_immed` linux-dmabuf различаются failure semantics. [linux-dmabuf][proto-dmabuf], [Wayland core][wayland-core].

## PART 19 — Performance и настоящий zero-copy

### 19.1. Строгая классификация

| Класс | Определение | Пример |
|---|---|---|
| TRUE ZERO COPY на участке bridge | Scanout использует тот же backing allocation без промежуточного pixel copy/render target | Compatible client dma-buf прямо на KMS plane; внутренние driver copies должны быть исключены доступными измерениями |
| ZERO CPU COPY | CPU не переносит pixel payload; GPU copies/draws допустимы | EGLStream texture → GL composition → output stream |
| GPU COPY / COMPOSITION | Чтение source и запись другого destination на GPU | Fullscreen textured triangle, blit или copy engine |
| CPU COPY | CPU читает/пишет pixel payload или выполняет upload/readback route | SHM → texture upload; software render → output staging |
| FORMAT / COLOR CONVERSION | Меняется представление или interpretation pixels | YUV→RGB, transfer function, bit depth, alpha conversion; может сочетаться с CPU/GPU copy |

Отсутствие `memcpy()` в N-Legacy не доказывает true zero-copy. Export/import FDs могут не копировать pixels, но subsequent composition копирует/перерисовывает содержимое в новый target.

**LEVEL 1 буквально** возможен при передаче references на тот же allocation с неизменёнными metadata и доказанным consumer support. Владение allocation не «переезжает»: добавляются references, а права доступа меняются через synchronization. На stream path невозможно обещать сохранение скрытого layout, если API его даже не раскрывает.

External texture fullscreen draw может остаться полностью GPU-side и часто быть дешёвым. Но `glBlitFramebuffer()` не универсален для stream external texture: external sampler обычно читается shader’ом; source не обязан быть attachable к read FBO. Для pixel identity требуются integer-aligned mapping, correct orientation/channel order, controlled filtering, alpha и color pipeline.

### 19.2. Нижняя оценка bandwidth

Для одного несжатого 32-bit full frame: `bytes = width × height × 4`. Таблица использует decimal MB/GB. Один дополнительный composition pass минимум читает source и пишет destination; scanout отдельно читает destination. Реальные traffic/latency могут быть выше из-за padding, caches, blending, overdraw и driver staging.

| Режим | MB/frame | Один поток pixels, GB/s | Read+write pass, минимум GB/s | Три полноразмерных buffers, MB |
|---|---:|---:|---:|---:|
| 1920×1080 @60 | 8.29 | 0.498 | 0.995 | 24.9 |
| 2560×1440 @60 | 14.75 | 0.885 | 1.769 | 44.2 |
| 3840×2160 @30 | 33.18 | 0.995 | 1.991 | 99.5 |
| 3840×2160 @60 | 33.18 | 1.991 | 3.981 | 99.5 |

При CPU readback + upload два пересечения CPU/GPU domain могут дать как минимум два таких pixel потока; software renderer добавляет CPU work и RAM traffic. Теоретическая PCIe/VRAM bandwidth не предсказывает реальные stalls, synchronization и effective upload rate.

На 2 GB VRAM простая пара output swapchains не главный потребитель: приложения, browser surfaces, textures и эффекты Hyprland тоже занимают память. На Xeon E3/8 GB software composition всего desktop может стать bottleneck раньше presentation. На 4K отдельно проверить, **поддерживает ли физический разъём конкретной ASUS-карты, кабель и монитор нужный mode**; bandwidth таблица не обещает 4K60 display capability.

### 19.3. Latency budget

60 Hz даёт 16.67 ms между refresh opportunities; 30 Hz — 33.33 ms. Второй compositor способен добавить ожидание следующего refresh, особенно при двух независимых FIFO queues. Это не обязательная фиксированная «+1 frame» для всех реализаций: зависит от scheduling и deadlines.

Начальная policy: максимум один in-flight presentation на output и один newest pending frame; после measurement выбрать bounded FIFO/mailbox. Измерять producer submit, consumer acquire, read complete, output submit, actual presentation, dropped frames, queue age и memory high-water marks. Оптимизировать damage/direct scanout только после корректности этих временных отношений.

## PART 20 — Testing и debugging

### 20.1. Диагностика по слоям

Команды ниже запускаются на Linux test machine; в текущем окружении они не выполнялись. Имена DRM nodes выбирать по результатам inventory, не копировать `card0` вслепую.

| Layer | Test | Что считать успехом |
|---|---|---|
| 1: PCI | `lspci -nnk` | Найден точный GPU ID и bound driver |
| 2: NVIDIA module | `cat /proc/driver/nvidia/version`, `modinfo nvidia`, `nvidia-smi` при поддержке | Module/userspace согласованы; ошибки NVML отдельно диагностированы |
| 3: KMS | `drm_info`, `modetest -M nvidia-drm -c -p`, read modeset sysfs parameter | Connected output, mode/CRTC/planes, требуемые properties; затем отдельный modeset test на test VT |
| 4: EGL | `eglinfo`, дополнительно собственный platform-specific probe | NVIDIA EGLDisplay на нужном device, не случайный X11/Mesa display |
| 5: EGLDevice | `nl-probe-device` — предлагаемый executable | Device↔DRM mapping + creation с master FD |
| 6: Stream | `nl-probe-stream` | Producer/consumer connect, frame acquire/release, disconnect без leak |
| 7: Wayland server | `wayland-info` и маленький SHM client | Globals, configure/ack, damage/commit/release |
| 8: Nested Hyprland | Stock binary + isolated config + logs | Нужный Wayland backend, работающий allocator/renderer |
| 9: Frame transport | `WAYLAND_DEBUG=1` на child, structured import log | Реальный linux-dmabuf buffer и metadata, а не только registry connection |
| 10: Renderer | Pattern/CRC test через проверенный importer | Correct orientation, channels, alpha, pitch и frame sequence |
| 11: Output | EGL/DRM event trace | Frame submitted и соответствующий completion/presentation |
| 12: Physical pixels | Фото/видео test pattern или display capture equipment | Нет tearing/corruption; экран соответствует ожидаемому кадру |

`weston-info` в старых инструкциях обычно заменяется `wayland-info`; наличие конкретной утилиты проверять по package. `glxinfo -B` полезен для i3/Xorg baseline, но не доказывает EGLDevice/Wayland. `kmscube` обычно проверяет GBM/KMS и может закономерно не работать там, где EGLStream вывод работает. `vulkaninfo --summary` — только Vulkan gate, не тест Aquamarine.

Дополнительные инструменты: `strace` для open/ioctl/FD lifecycle, `gdb` и core dumps, kernel journal/NVIDIA Xid, `nvidia-bug-report.sh`, EGL debug callback при наличии `EGL_KHR_debug`. DRM debug включать адресно на время теста: чрезмерные логи могут менять timing. Сохранять driver/module/package revisions вместе с trace.

### 20.2. Автоматизируемые tests

| Тип | Сценарии | Инвариант |
|---|---|---|
| Unit/property | Dimensions/stride/offset overflow, plane masks, duplicate plane, modifier negotiation | Invalid descriptor отвергается до unsafe arithmetic/import |
| Unit state machine | Cancel/disconnect на каждой стадии, повторный commit, два outputs | Ни early release, ни double release, ни потерянный usage ref |
| FD tests | Fault injection после каждого dup/import/allocation | FD count возвращается к baseline, CLOEXEC, нет double-close |
| Protocol integration | Empty initial commit, configure serials, bufferless commit, destroy pending resource | Нет protocol UAF; корректные errors/callbacks |
| Import matrix | Format × modifier × dimensions × exporter × importer × usage | Sample/render/export/scanout результаты записаны отдельно |
| Sync hardware | Delayed producer, long consumer draw, aggressive reuse | Ни смешанных frames, ни deadlock; release только после read completion |
| Pacing | Idle desktop, burst commits, slow output, mixed refresh | Bounded queue, нет busy loop/starvation, измеримые drops |
| Resize | Rapid grow/shrink, configure storm, resize при frame in flight | Старые generations корректно retire |
| Hotplug | Unplug при submit, reconnect с другим EDID/mode | Нет stale pointers/FB use; предсказуемая topology policy |
| Teardown | SIGTERM, client crash, server crash, context loss injection | Чистый доступный userspace cleanup и ограниченный restart |
| Session hardware | VT switch, lock, DPMS, suspend/resume | Нет утечки visible content, возврат input/output и full repaint |
| End-to-end apps | GTK/Qt, browser, Xwayland GLX/Vulkan, screen sharing | Работает boundary A, а не только картинка Hyprland |

CI без GTX 650 проверяет validation/state/protocol logic с mock backend. Mesa/VKMS CI не заменяет NVIDIA hardware tests. Sanitizers полезны для userspace lifetime; не все ошибки закрытого GPU driver доступны sanitizers.

## PART 21 — Risks and unknowns

| Риск / неизвестное | Приоритет | Как закрыть |
|---|---|---|
| Stock Hyprland native allocator/render/export на 470 | **Blocker** | P0 и подробные BO usage probes; не продолжать по одному successful EGL init |
| Приложения внутри Hyprland требуют неподдерживаемый EGLStream interface | **Blocker для desktop goal** | Реальные GL/EGL/Xwayland app tests; outer globals не лечат inner display |
| Mesa/software exporter на этой конфигурации | Высокий | Проверить всю GBM → render target → export/import цепочку |
| NVIDIA 470 EGL dma-buf import/modifiers/native fences | Высокий | Runtime queries + usage tests каждого сочетания |
| EGLOutput/flip integration на текущем kernel+patchset | Высокий | Native output probe и repeated presentation tests |
| Nested input, output discovery и timing limitations | Высокий для production | Проверить функции stock backend и UX tests; нельзя исправить только parent protocols |
| DKMS работает, но runtime KMS/suspend сломан | Высокий | Согласованный package snapshot и hardware regression suite |
| Закрытый driver hang / отсутствие поддерживаемого reset | Высокий | Bounded recovery, fallback session, признать возможность reboot |
| Suspend VRAM preservation и output recreation | Высокий | Hardware tests NVIDIA power-management configuration |
| Архивный Weston reference не полностью проверен | Средний | Отдельный audit конкретного commit при реальной необходимости его кода |
| API drift Hyprland/Aquamarine/Omarchy | Средний/высокий | Version matrix, smoke gate перед обновлением |
| Performance CPU route на Xeon/8 GB | Высокий | Frame-time measurements с реальными приложениями |

Противоречия, которые удалось разрешить:

- «470 поддерживает PRIME» и «470 не поддерживает GBM allocation/submission» совместимы: PRIME не равен полному GBM renderer contract.
- «Hyprland умеет EGLDevice» и «ему нужен GBM allocator» совместимы: EGLDisplay selection и output allocation — разные подсистемы.
- «wlroots-eglstreams mostly supports dma-buf» и «его stream buffers нельзя экспортировать» описывают разные направления sharing.
- «egl-wayland client-side library» и наличие server helper hooks совместимы: library не становится полноценным compositor.
- «Есть wl_shm в Aquamarine» не означает SHM desktop output: надо читать конкретный buffer path.

Утверждать production feasibility до закрытия первых двух blockers нельзя. В текущем состоянии это **исследовательский проект с проверяемыми gates**, а не подтверждённый compatibility product.

## PART 22 — Development roadmap

### 22.1. Порядок разработки с точками остановки

1. Зафиксировать hardware inventory, driver/package versions и рабочий i3 fallback.
2. Написать device/capability probe; сохранить JSON report.
3. Проверить stock Hyprland allocator на минимальном современном reference host и целевой 470-конфигурации. Зафиксировать **первый** failing stage.
4. Написать независимый GBM/EGL usage probe: allocate, render-target import, render, export, consumer import. Это дешевле большого compositor.
5. Если native route нет, выделить ограниченный эксперимент software exporter. Если он тоже не проходит — остановить Hyprland bridge work и пересмотреть ограничения.
6. Отдельно доказать KMS dumb output и EGLDevice/EGLOutput native pattern.
7. Добавить frame progress, errors и корректное завершение в output probe.
8. Создать минимальный Wayland server и SHM client test; реализовать configure/commit/release.
9. Показать SHM через native output; проверить orientation/channel order/stride.
10. Если нужен stream-native client, добавить EGL-Wayland bind/controller и acquire/release; проверить resize/disconnect.
11. Только при пройденном P0 добавить честный linux-dmabuf importer/feedback для Hyprland.
12. Получить P2: узнаваемый кадр именно stock Hyprland на физическом мониторе.
13. Проверить приложения внутри child display. При провале не переходить к Omarchy packaging.
14. Укрепить sync/lifetime, bounded scheduling, pacing, resize, fault handling.
15. Добавить physical input, keymaps, cursor и emergency controls.
16. Проверить VT transitions и lock policy до длительной эксплуатации.
17. Добавить per-output topology/hotplug, затем mixed refresh и suspend/resume.
18. Провести soak/fault/performance tests; только после них оптимизировать damage/direct scanout.
19. Сформировать Arch packages/session entry и update regression gate.
20. Интегрировать Omarchy scripts/portals/output policy и повторить end-to-end suite.

Это отличается от первоначального порядка: **sync/lifetime и session ownership начинаются до первого длительно работающего кадра**, а feasibility producer проверяется раньше сложного Wayland server.

### 22.2. Acceptance criteria

Числа ниже — предлагаемые стартовые критерии проекта, не результаты измерений и не отраслевой стандарт. Их можно ужесточить после baseline measurements.

| Уровень | Обязательный результат |
|---|---|
| Prototype P1 | Один physical monitor, reproducible native pattern, сохранён capability report, безопасное завершение normal path; честно обозначен как output-only |
| Prototype P2 | Stock Hyprland frame на мониторе; известные producer/import/sync paths; 20 последовательных запусков; отсутствие очевидной FD/resource утечки |
| Alpha | Один 1080p60 output, базовые apps/input/resize, 2 часа нагрузки, bounded queues; missed-frame counters и latency измерены; нет corruption/early reuse |
| Beta | 24 часа mixed workload, 100 resize/hotplug cycles и 30 VT/suspend cycles в заявленной конфигурации; multi-monitor только если включён в scope; recovery соответствует policy |
| Production для ограниченного профиля | Воспроизводимый package set, несколько дней реального использования, regression tests после updates, документированные app/input/output ограничения, проверенный lock/recovery; нет известных correctness blockers |

«Средние 60 fps» недостаточны: требуется distribution frame times, отсутствие growing queue и отсутствие незавершённых GPU reads при release. Production не обязан включать HDR/VRR/tablet, если ограничения честно опубликованы. Но невозможность аппаратно ускорять основные приложения или работать со stock nested backend должна отражаться в названии/обещаниях продукта, а не скрываться как мелкая настройка.

### 22.3. Reversibility решений

| Решение прототипа | Если путь окажется тупиком | Стоимость переделки |
|---|---|---|
| Отдельные capability/output/producer probes | Сохраняются как диагностика | Низкая |
| SHM первым frontend | Сохраняется как fallback/test transport | Низкая; не делать SHM обязательной общей моделью |
| Opaque EGLStream frame lease | Можно добавить dma-buf importer рядом | Низкая/средняя |
| Универсальный `nl_buffer` с обязательным FB/EGLImage/stream сразу | Придётся распутывать incompatible ownership | Высокая — избегать |
| Composition-only output | Direct scanout добавить как отдельную eligibility branch | Средняя |
| One-output prototype с output generation object | Добавление нескольких outputs без переписывания resource lifetimes | Средняя |
| Одна global width/height/current-buffer переменная | Multi-monitor/resize требуют перепроектирования | Высокая — избегать |
| Domain interfaces, static linking | Добавить DRM/GBM backend при необходимости | Низкая/средняя |
| Старый wlroots fork как основа всех objects | Переход на custom server перепишет glue и lifecycle | Высокая |
| Современный wlroots только как helper layer | Придётся заменить protocol adapters, но не всю domain model | Средняя |
| Software exporter как явно отдельный experimental profile | Можно удалить, сохранив host/output | Низкая; нельзя строить продуктовую гарантию на нём до измерений |

## PART 23 — Concrete module / API design

### 23.1. Структура проекта

Это **предлагаемая структура**, исходники в этой сессии не создавались. Для маленьких probes допустим плоский каталог. После роста — grouping по предметным областям; одна `.c/.h` пара на один компонент, `main.c` только wiring/entry point.

```text
n-legacy/
  src/
    main.c
    session/
      nl_session.c/.h
      nl_guest_process.c/.h
    desktop/
      nl_surface.c/.h
      nl_shell.c/.h
      nl_output_binding.c/.h
    content/
      nl_allocation.c/.h
      nl_usage.c/.h
      nl_dmabuf.c/.h
      nl_shm.c/.h
      nl_dependency.c/.h
    nvidia_display/
      nl_egl_device.c/.h
      nl_stream_input.c/.h
      nl_stream_output.c/.h
      nl_gl_composition.c/.h
      nl_output_import.c/.h
    display/
      nl_output.c/.h
      nl_kms_topology.c/.h
      nl_presentation.c/.h
      nl_frame_scheduler.c/.h
    input/
      nl_seat.c/.h
      nl_keyboard.c/.h
      nl_pointer.c/.h
    diagnostics/
      nl_capability_report.c/.h
      nl_frame_trace.c/.h
  probes/
  tests/
  protocols/
  packaging/
```

Не создавать общий `utils.c` или `backend.c`, где смешаны device probing, shell policy, GL import и input. Не вводить стабильный plugin ABI до второго реально существующего backend.

### 23.2. Объекты и responsibilities

| Объект | Содержит / делает | Не должен владеть |
|---|---|---|
| `nl_session` | Seat active state, device grants, activation notifications | GL textures и Wayland buffer policy |
| `nl_surface` | Pending/current protocol state, role, configure/commit serials, damage | Безусловным единственным глобальным GPU buffer |
| `nl_allocation` | Kind, immutable layout descriptor, source references | Simultaneously обязательными FB, GBM BO и EGLStream frame |
| `nl_usage` | Commit ID, allocation ref/frame lease, acquire dependency, consumer refs, release target | Чужим protocol resource после его destroy |
| `nl_dmabuf` | Validated width/height/FourCC/modifier, planes и owned FDs | Device-local handles всех возможных importers |
| `nl_stream_input` | Stream endpoint, consumer texture, connected/acquired state, generation | Общим dma-buf layout, которого API не дал |
| `nl_output_import` | Device-specific EGLImage/texture или GEM/FB view, cache key | Client allocation lifetime без явного reference |
| `nl_stream_output` | Output stream, producer EGLSurface, EGLOutput layer, presentation integration | Wayland surface shell state |
| `nl_output` | Mode/topology identity, scale/transform, generation, lifecycle | Всеми resources других outputs |
| `nl_presentation` | Submission ID, output generation, completion/presentation data | Разрешением reuse без соответствующего dependency |
| `nl_frame_scheduler` | Bounded pending queue, deadlines, pacing policy | Реализацией GPU fence как обычного boolean |

### 23.3. Концептуальные data contracts

Следующий фрагмент — data sketch, не готовый компилируемый header. Referenced types определяются в собственных модулях.

```c
struct nl_dmabuf_plane {
    int fd;                 /* owned descriptor, close exactly once */
    uint32_t offset;
    uint32_t stride;
};

struct nl_dmabuf_desc {
    uint32_t width, height;
    uint32_t fourcc;
    uint64_t modifier;
    uint32_t plane_count;
    struct nl_dmabuf_plane planes[4];
};

struct nl_usage {
    uint64_t commit_id;
    struct nl_allocation *allocation;
    struct nl_dependency *acquire;
    struct nl_release_target *release;
    unsigned int active_consumers;
    bool protocol_resource_alive;
    bool retired;
};
```

Один modifier здесь соответствует negotiated image layout contract; не универсальное обещание всех будущих graphics APIs. Для stream source `allocation` должен быть tagged opaque resource/lease, а не фиктивный `nl_dmabuf_desc` с `fd=-1`.

`nl_dependency` — tagged type: completed, implicit-contract reference, sync-file, timeline point либо stream-acquire contract. Его operations `is_ready`, `wait_supported`, `export_supported` имеют capability semantics. Нельзя реализовать любой вариант через одинаковый `poll(fd)`.

Основные интерфейсы:

```text
probe(device) -> capability_report
validate_input(descriptor, limits) -> validated_content | rejection
import_for_sampling(content, graphics_domain) -> sample_view | unsupported
acquire_usage(surface_commit) -> usage
compose(usage, output_generation) -> read_completion + output_job
present(output_job) -> presentation_token
retire(presentation_token) -> output references released
release_usage(usage) -> protocol release when eligible
```

### 23.4. Ownership rules для реализации

Все Wayland resources и shell state изменяет owner event thread. GL context принадлежит одному graphics thread; inter-thread handoff содержит immutable descriptors и refs, не raw pointers на уничтожаемые resources. Import caches ключуются device/context generation и allocation identity; повторно использованный integer FD не является устойчивым cache key.

Каждый error return сопровождается cleanup ledger/понятным owner. Callback отменяется либо держит reference. State transition проверяет generation. Производственные asserts — только для внутренних инвариантов, никогда вместо validation входа.

Если нужен ручной бесконечный event loop, его форма в C — `for (;;)`. Обычно разумнее подключить sources к уже выбранному event loop, не создавать второй polling loop с собственной несогласованной жизнью.

## PART 24 — Independent re-evaluation from zero

Теперь исходные вводные сведены к четырём требованиям: **modern Hyprland, GTX 650, proprietary 470, no Hyprland/Aquamarine changes**.

1. Hyprland должен сначала создать renderable output buffers. Это необходимо до любого внешнего presentation bridge.
2. Проверенный Aquamarine выделяет output storage через GBM и передаёт dma-buf. NVIDIA 470 не предоставляет документированный native GBM backend.
3. Значит, добавление внешнего EGLStream compositor не является достаточным решением.
4. Если нельзя менять Hyprland/Aquamarine, остаётся найти **реально совместимый существующий producer stack**, а не переименовать output protocol.
5. На единственной legacy карте это ведёт к узкому исследованию Mesa/software allocation/export и cross-implementation import. Работоспособность и performance неизвестны.
6. Даже успешный producer не гарантирует acceleration приложений на внутреннем display. Поэтому production goal шире first-frame bridge.

| Запрошенный итог | Независимый ответ |
|---|---|
| Best architecture при всех фиксированных ограничениях | Подтверждённой полноценной аппаратно ускоренной архитектуры пока нет; conditional standard-dmabuf host + legacy presentation имеет смысл только после producer gate |
| Alternative architecture | Для эксперимента — software producer + CPU-readable transport + NVIDIA output; для практичного modern desktop — Nouveau/Mesa или совместимый GPU с явным изменением одного требования |
| Minimal prototype | Allocator/render/export probe + независимый EGLDevice/EGLOutput pattern, затем их соединение |
| Production architecture | При доказанном compatible producer — restricted outer compositor, composition-only, explicit ownership и bounded scheduling; при отсутствии producer — не объявлять N-Legacy production solution |
| Biggest technical blocker | Allocation/render/export contract stock Hyprland на 470 |
| Biggest unknown | Существует ли приемлемый real software/Mesa producer и безопасный transfer на этой exact configuration |
| Biggest risk | Потратить месяцы на output compositor, а затем обнаружить нерешаемые producer и application-side barriers |
| Recommended test order | Producer gate → native output → import/sync → actual Hyprland frame → actual apps → lifecycle/input → integration |

Сравнение с исходной идеей: **outer compositor как граница presentation остаётся разумным**, но предположение об automatic EGLStream transport от stock Hyprland отвергнуто. В исправленной архитектуре вход — то, что реально выдаёт клиент: linux-dmabuf. EGLStream находится на output стороне либо обслуживает отдельного действительно stream-aware client. Это существенное изменение feasibility, а не косметическое переименование модулей.

## PART 25 — Final technical conclusion

N-Legacy можно спроектировать как самостоятельный legacy presentation compositor. Документированный NVIDIA 470 output path и исторические реализации дают для этого реальную основу. Клиентские buffers не обязаны становиться DRM FB; GL composition позволяет отделить input storage от scanout storage.

Однако **не доказано, что такой компонент позволит указанной машине использовать современный stock Hyprland/Omarchy с нормальным аппаратным ускорением**. Наиболее важная предпосылка исходной схемы — EGLStream-producing nested Hyprland — противоречит проверенному коду Aquamarine. Kernel patches, наличие Wayland socket, controller global и успешный NVIDIA EGL context не закрывают этот разрыв.

Практическое решение: инвестировать сначала в небольшие falsifiable probes. При успешном producer gate строить узкий composition host. При неуспехе честно пересмотреть driver/hardware/producer constraints, сохранив написанные probes и legacy output backend, а не расширять невыполнимое обещание.

---

## A. Things we thought were true but turned out false

1. «Любой nested Hyprland использует NVIDIA Wayland EGL window-surface integration» — проверенный Aquamarine создаёт dma-buf buffers самостоятельно.
2. «EGLStream-capable parent автоматически переключит child на EGLStream» — protocol registry не меняет allocation/render code клиента.
3. «Достаточно решить последний output участок» — остаются producer и application-side contracts.
4. «`wl_shm` в backend означает software fallback рабочего стола» — найденный SHM path относится к cursor.
5. «EGLStream FD можно импортировать как dma-buf» — это разные FD contracts.
6. «Любой EGLImage можно экспортировать, а stream frame всегда даёт EGLImage» — нет таких универсальных гарантий.
7. «PRIME support означает arbitrary dma-buf scanout» — import, render и scanout проверяются отдельно.
8. «Frame callback означает, что buffer свободен или физически показан» — это отдельные события.
9. «Новый kernel/libgbm добавляет NVIDIA 470 native GBM» — нет.
10. «i3/Xorg и N-Legacy могут независимо управлять тем же output одновременно» — нужен согласованный session/device ownership.

## B. Things that remain unknown until tested on GTX 650

- Exact EGL extensions для каждого выбранного platform/display и их usable formats/modifiers.
- Работоспособность всей Mesa/software producer цепочки с unchanged Hyprland.
- NVIDIA import конкретных exported allocations как texture и как render target.
- PRIME import/export/scanout конкретных buffer types; наличие usable fences/syncobj.
- EGLOutput layer/flip event behavior на выбранном kernel/AUR patchset.
- Suspend/resume, VT, hotplug и recovery после ошибок.
- Реальные frame times, latency, RAM/VRAM growth и CPU transfer cost.
- Поведение требуемых приложений внутри Hyprland.
- Поддержанные modes конкретной ASUS board/output/cable/monitor.

## C. Most promising architecture for first prototype

**Два малых теста и gate между ними:** stock-producer allocation/render/export test и независимый GL → EGLStream → EGLOutput native output test. После их успеха — minimal libwayland-server host с SHM diagnostic input и проверенным dmabuf importer. EGLStream input добавляется только для отдельного stream-aware test client, не как ожидаемый transport Hyprland.

## D. Most promising architecture for stable production

При всех исходных ограничениях production path пока не подтверждён. Если совместимый producer и реальные приложения проходят tests — ограниченный outer compositor, один composition path, stream output per monitor, session/input lifecycle и versioned capability profiles.

Если приоритет — надёжный modern Hyprland workflow, архитектурно проще устранить driver incompatibility через проверенный Nouveau/Mesa либо подходящее оборудование. Это изменение требований, а не скрытая «реализация 470 compatibility».

## E. Exact first 10 implementation tasks

1. Создать Meson C project для probes с warnings и отдельными component files.
2. Реализовать machine/version/capability JSON report.
3. Реализовать EGLDevice↔DRM matching и platform-specific EGL query.
4. Реализовать GBM BO allocation/export probe с полным metadata log.
5. Добавить EGL import **как render target**, patterned rendering и export/consumer check.
6. Реализовать самостоятельный DRM dumb modeset/restore probe.
7. Реализовать EGLStream producer→EGLOutput pattern с bounded progress/error reporting.
8. Создать minimal Wayland compositor/xdg-shell/seat/SHM server и test client.
9. Добавить opaque usage refs, completion handling, SHM upload и native output integration.
10. **Только при successful producer gate:** добавить valid linux-dmabuf v4 feedback/import и подключить stock Hyprland; при провале оформить no-go report вместо фиктивной реализации.

## F. Exact first 10 experiments to run on hardware

1. Снять PCI IDs, module/userspace versions и i3/Xorg GL baseline.
2. Проверить KMS resources/properties и смену ownership между Xorg VT и test VT.
3. На нужном EGLDevice получить extensions/configs, создать GL context и завершить GPU pattern render.
4. Проверить native и доступный Mesa GBM paths: allocate → export всех planes → print metadata; фиксировать failures.
5. Проверить **renderability**, sampling и повторный import exported buffer отдельно, со счётчиком frames.
6. Запустить неизменённый Hyprland на минимальном parent и установить точную точку отказа; проверить software candidate только отдельным профилем.
7. Вывести dumb-buffer pattern напрямую KMS; это отделяет display/modeset проблему от EGL.
8. Вывести native EGLStream/EGLOutput pattern; проверить swap/acquire/event ordering.
9. Передать SHM и NVIDIA EGLStream test-client patterns в N-Legacy; испытать resize/disconnect и FD baseline.
10. При пройденных gates показать actual Hyprland frame, затем delayed-producer/aggressive-reuse test и первое аппаратно ускоренное приложение внутри child display.

## G. Things we absolutely should NOT implement yet

- Global fake GBM, перехват множества libEGL/libgbm calls и подмена library versions.
- Универсальный EGLStream→dma-buf converter без доказанного exporter.
- Полный Omarchy installer до producer и application gates.
- Direct scanout, overlays, HDR, VRR, complex color management, VR streaming и remote transport.
- Vulkan/CUDA bridge без выявленной необходимости и подтверждённых external-memory APIs.
- General compositor plugin ABI, поддержка всех legacy vendors и гигантский abstraction framework.
- Непроверенные explicit-sync globals или fake dmabuf feedback ради прохождения startup checks.
- Обещание transparent crash recovery или «zero-copy» без доказательства allocation/sync path.

---

## Покрытие вопросов ASK.md

| № | Где дан ответ | № | Где дан ответ |
|---|---|---|---|
| 1 | PART 4 | 29 | PART 12, 14 |
| 2 | PART 6 | 30 | PART 15 |
| 3 | PART 5 | 31 | PART 2, 7, 16 |
| 4 | PART 5.3–5.4 | 32 | PART 18 |
| 5 | PART 3 | 33 | PART 19 |
| 6 | PART 10.1 | 34 | PART 10–12, 23 |
| 7 | PART 9 | 35 | PART 23 |
| 8 | PART 4, 19 | 36 | PART 11.3–11.4 |
| 9 | PART 7.6 | 37 | PART 11.5 |
| 10 | PART 7 | 38 | PART 14 |
| 11 | PART 13 | 39 | PART 16 |
| 12 | PART 5.5, 6, 11.3 | 40 | PART 10, 16, 22 |
| 13 | PART 11 | 41 | PART 17 |
| 14 | PART 12, 22 | 42 | PART 2.1, 17 |
| 15 | PART 3.4, 11.4, 16.3–16.4 | 43 | PART 6.4, 14, 19 |
| 16 | PART 20.1 | 44 | PART 12.4, 23 |
| 17 | PART 16.2 | 45 | PART 1, 21, 24–25 |
| 18 | PART 16.1 | 46 | PART 8 |
| 19 | PART 9–10 | 47 | PART 8, 23 |
| 20 | PART 8.5 | 48 | PART 20.2 |
| 21 | PART 2.2 | 49 | PART 22.1, E–F |
| 22 | PART 6.2–6.4 | 50 | PART 22.2 |
| 23 | PART 3.3, 4.3, 11 | 51 | PART 22.3 |
| 24 | PART 4.3, 5, 19.1 | 52 | PART 2.3, 6.5 |
| 25 | PART 12.2 | 53 | PART 19.1 |
| 26 | PART 13.4, 15.1 | 54 | PART 10.1, 12 |
| 27 | PART 15.2 | 55 | PART 24–25 |
| 28 | PART 7, 13.5 | — | Приложения A–G фиксируют итоговые решения |

## Первоисточники

Ниже определения ссылок, используемых в документе. Стандарты подтверждают semantics API; они не доказывают runtime support NVIDIA 470. Исторический код подтверждает существование алгоритма в указанной версии, а не успешный запуск на сегодняшнем Arch.

Основные группы первоисточников:

- **Stock compositor:** [Hyprland startup][hypr-compositor], [renderer][hypr-gl], [protocols][hypr-protocols], [Aquamarine Wayland][aq-wayland], [allocator][aq-gbm], [backend core][aq-backend].
- **NVIDIA:** [470 KMS][nv-kms], [supported chips][nv-chips], [driver components][nv-components], [power management][nv-power], [495 GBM][nv-gbm].
- **EGL-Wayland integration:** [README][egl-readme], [surface lifecycle][egl-surface], [server helpers][egl-server], [hooks][egl-exports], [controller XML][egl-controller], [stream XML][egl-stream-xml].
- **Khronos:** [stream][spec-stream], [GL consumer][spec-gl-consumer], [cross-process transport][spec-cross-process], [output consumer][spec-egloutput], [DRM mapping][spec-device-drm], [modifiers][spec-modifiers], [native fences][spec-native-fence].
- **Linux/Wayland:** [DRM UAPI][drm-uapi], [dma-buf][kernel-dmabuf], [Wayland core][wayland-core], [linux-dmabuf][proto-dmabuf], [syncobj][proto-syncobj], [explicit sync][proto-explicit], [presentation][proto-presentation].
- **Исторические реализации:** [KWin EGLStream][kwin-egl], [KWin DRM][kwin-drm], [wlroots fork README][wlr-readme], [stream allocator][wlr-allocator], [DRM renderer][wlr-drm-renderer].
- **Distribution/integration:** [AUR metadata][aur-rpc], [PKGBUILD][aur-pkgbuild], [Omarchy NVIDIA setup][omarchy-nvidia], [Mesa GBM][mesa-gbm].
- **Лицензии:** [wlroots][wlr-license], [Weston][weston-license], [Mesa][mesa-license], [KWin file-level SPDX][kwin-egl], [egl-wayland][egl-readme].

[aq-wayland]: https://github.com/hyprwm/aquamarine/blob/7bb8bdf4e8fedaf4dfae58512bc4c728671727df/src/backend/Wayland.cpp
[aq-backend]: https://github.com/hyprwm/aquamarine/blob/7bb8bdf4e8fedaf4dfae58512bc4c728671727df/src/backend/Backend.cpp
[aq-gbm]: https://github.com/hyprwm/aquamarine/blob/7bb8bdf4e8fedaf4dfae58512bc4c728671727df/src/allocator/GBM.cpp
[aq-headless]: https://github.com/hyprwm/aquamarine/blob/7bb8bdf4e8fedaf4dfae58512bc4c728671727df/src/backend/Headless.cpp
[hypr-compositor]: https://github.com/hyprwm/Hyprland/blob/e368c13c27a42a173b9e08fa0bf413f9f7073187/src/Compositor.cpp
[hypr-gl]: https://github.com/hyprwm/Hyprland/blob/e368c13c27a42a173b9e08fa0bf413f9f7073187/src/render/OpenGL.cpp
[hypr-protocols]: https://github.com/hyprwm/Hyprland/blob/e368c13c27a42a173b9e08fa0bf413f9f7073187/src/managers/ProtocolManager.cpp
[nv-kms]: https://download.nvidia.com/XFree86/Linux-x86_64/470.256.02/README/kms.html
[nv-chips]: https://download.nvidia.com/XFree86/Linux-x86_64/470.256.02/README/supportedchips.html
[nv-components]: https://download.nvidia.com/XFree86/Linux-x86_64/470.256.02/README/installedcomponents.html
[nv-gbm]: https://download.nvidia.com/XFree86/Linux-x86_64/495.44/README/gbm.html
[nv-power]: https://download.nvidia.com/XFree86/Linux-x86_64/470.256.02/README/powermanagement.html
[egl-readme]: https://github.com/NVIDIA/egl-wayland/blob/1.1.7/README.md
[egl-exports]: https://github.com/NVIDIA/egl-wayland/blob/1.1.7/src/wayland-external-exports.c
[egl-display]: https://github.com/NVIDIA/egl-wayland/blob/1.1.7/src/wayland-egldisplay.c
[egl-surface]: https://github.com/NVIDIA/egl-wayland/blob/1.1.7/src/wayland-eglsurface.c
[egl-swap]: https://github.com/NVIDIA/egl-wayland/blob/1.1.7/src/wayland-eglswap.c
[egl-server]: https://github.com/NVIDIA/egl-wayland/blob/1.1.7/src/wayland-eglstream-server.c
[egl-stream-xml]: https://github.com/NVIDIA/egl-wayland/blob/1.1.7/wayland-eglstream/wayland-eglstream.xml
[egl-controller]: https://github.com/NVIDIA/egl-wayland/blob/1.1.7/wayland-eglstream/wayland-eglstream-controller.xml
[spec-stream]: https://registry.khronos.org/EGL/extensions/KHR/EGL_KHR_stream.txt
[spec-gl-consumer]: https://registry.khronos.org/EGL/extensions/KHR/EGL_KHR_stream_consumer_gltexture.txt
[spec-cross-process]: https://registry.khronos.org/EGL/extensions/KHR/EGL_KHR_stream_cross_process_fd.txt
[spec-egloutput]: https://registry.khronos.org/EGL/extensions/EXT/EGL_EXT_stream_consumer_egloutput.txt
[spec-device-drm]: https://registry.khronos.org/EGL/extensions/EXT/EGL_EXT_device_drm.txt
[spec-modifiers]: https://registry.khronos.org/EGL/extensions/EXT/EGL_EXT_image_dma_buf_import_modifiers.txt
[spec-native-fence]: https://registry.khronos.org/EGL/extensions/ANDROID/EGL_ANDROID_native_fence_sync.txt
[wayland-core]: https://gitlab.freedesktop.org/wayland/wayland/-/blob/main/protocol/wayland.xml
[proto-dmabuf]: https://gitlab.freedesktop.org/wayland/wayland-protocols/-/blob/main/stable/linux-dmabuf/linux-dmabuf-v1.xml
[proto-explicit]: https://gitlab.freedesktop.org/wayland/wayland-protocols/-/blob/main/unstable/linux-explicit-synchronization/linux-explicit-synchronization-unstable-v1.xml
[proto-syncobj]: https://gitlab.freedesktop.org/wayland/wayland-protocols/-/blob/main/staging/linux-drm-syncobj/linux-drm-syncobj-v1.xml
[proto-presentation]: https://gitlab.freedesktop.org/wayland/wayland-protocols/-/blob/main/stable/presentation-time/presentation-time.xml
[drm-uapi]: https://docs.kernel.org/gpu/drm-uapi.html
[kernel-dmabuf]: https://docs.kernel.org/driver-api/dma-buf.html
[kwin-egl]: https://github.com/KDE/kwin/blob/v5.22.0/src/plugins/platforms/drm/egl_stream_backend.cpp
[kwin-drm]: https://github.com/KDE/kwin/blob/v5.22.0/src/plugins/platforms/drm/drm_output.cpp
[wlr-readme]: https://github.com/danvd/wlroots-eglstreams/blob/master/README.md
[wlr-allocator]: https://github.com/danvd/wlroots-eglstreams/blob/master/render/eglstreams_allocator.c
[wlr-drm-renderer]: https://github.com/danvd/wlroots-eglstreams/blob/master/backend/drm/renderer.c
[wlr-license]: https://github.com/danvd/wlroots-eglstreams/blob/master/LICENSE
[weston-history]: https://cgit.freedesktop.org/~jjones/weston/
[weston-license]: https://gitlab.freedesktop.org/wayland/weston/-/blob/main/COPYING
[mesa-license]: https://gitlab.freedesktop.org/mesa/mesa/-/blob/main/docs/license.rst
[mesa-gbm]: https://gitlab.freedesktop.org/mesa/mesa/-/blob/main/src/gbm/backends/dri/gbm_dri.c
[aur-package]: https://aur.archlinux.org/packages/nvidia-470xx-utils
[aur-rpc]: https://aur.archlinux.org/rpc/v5/info?arg%5B%5D=nvidia-470xx-utils&arg%5B%5D=nvidia-470xx-dkms
[aur-pkgbuild]: https://aur.archlinux.org/cgit/aur.git/plain/PKGBUILD?h=nvidia-470xx-utils
[omarchy-nvidia]: https://github.com/omacom/omarchy/blob/dev/install/config/hardware/nvidia.sh

Дополнительные прочитанные источники: [NVIDIA 470 Xwayland requirements](https://download.nvidia.com/XFree86/Linux-x86_64/470.256.02/README/xwayland.html), [EGL dma-buf export specification](https://registry.khronos.org/EGL/extensions/MESA/EGL_MESA_image_dma_buf_export.txt), [EGL output base](https://registry.khronos.org/EGL/extensions/EXT/EGL_EXT_output_base.txt), [wlroots historical EGL integration](https://github.com/danvd/wlroots-eglstreams/blob/master/render/egl.c), [Nouveau power-management documentation](https://nouveau.freedesktop.org/PowerManagement.html).

**Граница результата:** выполнен source/API/architecture review и подготовлен план реализации. Код N-Legacy, hardware probes и испытания GTX 650 в рамках этого ответа не выполнялись. Все feasibility gates, требующие hardware, явно оставлены открытыми.
