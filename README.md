# Control Tower

Preamp digital de sound system para tocar en vivo desde la Mac, sin conexión: dos decks, mixer con
EQ aislador, aislador master de 3/4/5 vías, tape echo, spring, sirenas, 16 samples, grabación de la
sesión. Viene mapeado de fábrica para la **Pioneer DDJ-FLX4**, y cualquier otro controlador se asigna
con MIDI learn.

Corre en macOS 10.13 o superior (incluye Catalina en la MacBook Pro mid 2012) y en Apple Silicon.

## Instalar

1. Descargá `ControlTower.dmg`: desde **Releases**, o desde la pestaña **Actions** (última ejecución
   verde, abajo en *Artifacts* → `ControlTower-macOS`).
2. Abrí el .dmg y arrastrá **Control Tower** a **Aplicaciones**.
3. La primera vez: **clic derecho → Abrir → Abrir** (no está firmada por Apple).
4. Aceptá el permiso de micrófono: es para la entrada en vivo (MIC / LINE).

## Conexión con la DDJ-FLX4

```
Mac ── USB ── DDJ-FLX4 ── MASTER OUT ──► mesa (2 canales de la MP-7, como siempre)
                       └─ PHONES ──────► auriculares (cue)
```

- En **AUDIO / SALIDAS** elegí la FLX4 como placa. Con 4 salidas, Control Tower manda el
  **master a las salidas 1-2** y los **auriculares a las 3-4** (se ajusta ahí mismo si hace falta).
- La FLX4 se detecta sola como controlador. **rekordbox tiene que estar cerrado**.

## Mapeo de fábrica de la FLX4

| Control de la FLX4 | En Control Tower |
|---|---|
| BROWSE (girar) | recorre la lista con foco (carpetas o temas); con SHIFT, de a 10 |
| BROWSE (apretar) | cambia el foco entre carpetas y temas |
| LOAD 1 / LOAD 2 | carga el tema elegido en el deck 1 / 2 (no carga si el deck está sonando) |
| PLAY / CUE | play/pausa y cue estilo CDJ (en pausa: marca y preescucha mientras lo mantenés) |
| SHIFT + CUE | volver al principio |
| JOG (plato) | tocado: scratch / mover el tema; sin tocar o borde: empujar el pitch |
| SHIFT + JOG | búsqueda rápida |
| TEMPO | pitch (±8%; SHIFT + BEAT SYNC cambia a ±16 / ±50) |
| BEAT SYNC | **tap tempo** del echo |
| LOOP IN / OUT / RELOOP, CUE/LOOP CALL ◀ ▶ | loop manual, salir/volver, mitad / doble |
| TRIM, HI, MID, LOW | trim y EQ aislador del canal (al mínimo = kill de la banda) |
| CFX | filtro del canal (izquierda pasa-bajos, derecha pasa-altos) |
| Faders, crossfader | como siempre |
| CUE de canal, HEADPHONES MIX | cue en auriculares y mezcla cue / master |
| BEAT FX LEVEL/DEPTH | repeticiones del echo |
| BEAT FX ON/OFF | envío fijo al echo (SHIFT: envío fijo al spring) |
| BEAT FX SELECT | kill del echo |
| BEAT ◀ ▶ | división del echo |
| Selector CH 1 / CH 2 de BEAT FX | qué canal manda al echo y al spring (en el medio: los dos) |

### Pads (8 por lado, según el modo)

| Modo del pad | Pads 1 a 8 |
|---|---|
| **HOT CUE** | hot cues 1-8 (SHIFT + pad: borrar) |
| **KEYBOARD** → *dub* | 1 dos tonos · 2 láser · 3 ondulante · 4 caída · 5 sub drop · 6 throw al echo · 7 throw al spring · 8 freeze del echo (todos se mantienen) |
| **BEAT JUMP** → *aislador* | 1-5 kill de cada banda · 6 FLAT · 7 SUB BOOST · 8 kill del echo |
| **BEAT LOOP** → *tiempo del echo* | 1/16 · 1/8 · 1/8· · 1/4 · 1/4· · 1/2 · 1/4T · TAP |
| **SAMPLER** | samples 1-8 (izquierda) y 9-16 (derecha); SHIFT + pad: parar |

Las luces de la FLX4 siguen el estado: play, cue, loops, hot cues marcados, samples cargados,
kills y sirenas activas, y los vúmetros de canal.

## Teclado

| Tecla | Acción |
|---|---|
| Z X C V B (mantener) | sirenas 1 a 5 |
| E / R (mantener) | throw al echo / al spring |
| F (mantener) | freeze del echo |
| K / T | kill del echo / tap tempo |
| 1 a 7 | divisiones del echo |
| Q / P | play deck 1 / deck 2 |
| ↑ ↓ ← → | biblioteca: mover / carpetas / temas |
| [ / ] | cargar en deck 1 / deck 2 |

## Módulos

- **Decks**: el tema se decodifica en segundo plano a la memoria (unos 10 MB por minuto). Los
  saltos de cue, hot cues, loops y scratch son instantáneos. Sólo se guardan en memoria los dos temas
  cargados, nunca la biblioteca entera. Arrastrá temas desde el Finder o desde la biblioteca a un deck.
- **Mixer**: trim, EQ aislador de 3 bandas con kill (crossover Linkwitz-Riley, suma plana), filtro,
  fader, crossfader, cue.
- **Aislador master**: 3, 4 o 5 vías (cambia en vivo sin clicks), ganancia y kill por banda, FLAT,
  SUB BOOST.
- **Tape echo**: el motor de Dub Echo: inercia de cinta (cambiar el tiempo dobla el pitch), wow,
  saturación, filtros en el loop, feedback hasta 110%, freeze y kill. Envío por throw, envío fijo o
  perilla.
- **Spring**: reverb de resortes con cuerpo, cola, tono, y entrada desde el echo.
- **Sirenas**: dos tonos, láser, ondulante, caída y sub drop, con tono y velocidad.
- **Samples**: 16 ranuras. Clic derecho o arrastrar un archivo para cargar. Se recuerdan al reabrir.
- **Entrada en vivo**: micrófono o línea de la placa, con envío al echo.
- **Master**: filtro con resonancia, drive, nivel, limitador, vúmetros y **REC**. La grabación queda
  en *Música / Control Tower* en WAV.

Todo (valores, mapeos MIDI, carpetas de la biblioteca, samples) se guarda solo.

## Compilar

GitHub compila en cada push (`.github/workflows/build-macos.yml`), corre las pruebas y genera el .dmg.

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCT_TESTS=ON
cmake --build build -j 4
./build/EngineTest_artefacts/Release/EngineTest out        # prueba el motor y genera out/preamp_demo.wav
c++ -O2 -std=c++17 -ISource tests/dsp_test.cpp -o dsp_test && ./dsp_test
```

Estructura: `Dsp.h` (aislador, filtros, spring, dinámica), `Deck.h`, `TapeEchoDSP.h` y `SirenDSP.h`
(de Dub Echo), `Sampler.h` no dependen de JUCE. `Engine` arma la cadena de audio, `Controller`
el MIDI y las luces, `Views.h` la pantalla.

El mapeo de la FLX4 usa la tabla de mensajes MIDI del proyecto libre Mixxx. El prototipo anterior en
Electron quedó documentado en `docs/prototipo-electron-HANDOFF.md`.
