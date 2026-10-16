# HANDOFF — Control Tower (preamp digital offline)

Este documento es para pasarle el proyecto a otro asistente/modelo
(ej. Kimi K3) y que pueda seguir sin perder el contexto de las
decisiones ya tomadas. Dale este archivo + la carpeta completa del
proyecto (o el zip descomprimido).

## Qué es esto

Preamp digital offline al estilo de **AmpFreQQ** y del **WAMI Preamp**
(wamirecords.com/es/wami-preamp) — un pre amplificador que emula los
que se usan en soundsystems reales de dub/reggae. App de escritorio,
sin conexión a internet requerida.

## Hardware objetivo

- **MacBook Pro Mid 2012** (tope oficial: macOS Catalina 10.15.7)
- Controlador MIDI: **Novation Launch Control XL** (class-compliant USB-MIDI)
- Desarrollo/pruebas se están haciendo en Windows, el build final para
  Mac hay que compilarlo corriendo en macOS (o CI con runner Mac).

## Decisiones de arquitectura y por qué

- **Electron, no Tauri**: Safari/WebKit nunca implementó la Web MIDI
  API. Tauri usa el WebView del sistema en Mac (WebKit), así que no
  se podría leer el Launch Control XL ahí. Electron empaqueta
  Chromium, que tiene Web MIDI y Web Audio completos.
- **Web Audio API nativa** para todo el DSP (BiquadFilter, Delay,
  Convolver, Gain) en vez de AudioWorklet/JS a mano — clave para que
  rinda en el dual-core de 2012. Solo se usa JS puro donde no hay
  nodo nativo que lo resuelva.
- **Versión de Electron fijada, a revisar antes del build final**:
  hoy en `package.json` está en `^28.3.3`. Antes de compilar para el
  Mac 2012 hay que confirmar que esa versión (o una compatible) todavía
  corre en Catalina.

## Referencia de diseño funcional (WAMI Preamp)

El dueño del proyecto mandó una captura de la interfaz de WAMI Preamp
como referencia de qué módulos tiene que tener la app (el diseño
visual final lo define él aparte, esto es solo para no olvidar
funcionalidad):

- Lista de temas + bandeja con waveform L/R y plato tipo turntable
  (play/rewind/loop/mic live/pitch), con pestañas 1/2/3 para varios
  temas cargados en la misma bandeja.
- Isolator con selector de vías conmutable (3/4/5 bandas), botones
  FLAT y SUB BOOST, kill por banda.
- Tape echo: subdivisiones sincronizadas a tempo (1/16, 1/8D, 1/4,
  1/2), tap tempo, tiempo, repeticiones, tono, envío, feedback, kill.
- Spring reverb: envío, tamaño/tono de tanque (CUERPO), kill.
- Dub siren rack: 3 modos (dub siren / laser / sub drop), tono,
  velocidad, barrido, envío al echo, volumen, kill.
- Rack de 24 pads one-shot con envío al echo.
- Master: barrido, drive, volumen, tags de estado.
- Botón de MIDI Learn.

## Qué está construido ya (funcional)

- **INPUT**: selección de dispositivo + ganancia.
- **ISOLATOR**: 4 bandas fijas (SUB/BASS/MID/TOP) con kill. *(Falta el
  selector 3/4/5 vías + FLAT + SUB BOOST.)*
- **RESONANT FILTER**: cutoff, resonancia, tipo conmutable.
- **TAPE ECHO**: completo — sync a tempo, tap tempo, tono en el lazo
  de feedback, freeze, kill, envío "caliente".
- **SPRING REVERB**: completo pero *algorítmico* (ruido + resonancia
  en la frecuencia de CUERPO), no es convolución con IR real todavía.
- **DUB SIREN RACK**: completo — 3 modos, envío al echo, kill.
- **PLAYLIST + DECK**: completo — drag & drop / selector de archivos,
  3 bandejas (pestañas 1/2/3), waveform L/R con playhead animado
  (cacheada como ImageData para no recalcular samples en cada frame,
  importante para el CPU del 2012), play/pause/rewind/loop/pitch,
  MIC LIVE para mezclar la entrada física con la reproducción.
- **MASTER**: VU meter + nivel de salida.
- **MIDI Learn**: click en MIDI LEARN, click en un knob o click derecho
  en un botón toggle, mover el control físico → queda mapeado. No
  persiste entre sesiones todavía.

## Qué falta

- Isolator con vías 3/4/5 + FLAT + SUB BOOST.
- Rack de 24 pads one-shot con envío al echo.
- Guardado de presets (mapeo MIDI + valores) a JSON local.
- Empaquetado final con `electron-builder` para Windows y Mac
  (verificar compatibilidad de la versión de Electron con Catalina).
- El diseño visual final — el layout actual es una grilla auto-fit
  provisoria, el dueño del proyecto va a definir la estética.

## Bugs ya resueltos (para no repetirlos)

- **AudioContext suspendido por política de autoplay de Chromium**:
  se creaba el `AudioContext` al cargar la página, antes de cualquier
  gesto del usuario, y quedaba silenciado. Se resuelve escuchando
  `mousedown`/`keydown` a nivel `window` y llamando `audioCtx.resume()`.
  Ya está aplicado — si el audio vuelve a fallar en silencio, revisar
  esto primero.
- Un fallo en el permiso de audio de entrada (`getUserMedia`) cortaba
  la inicialización de MIDI porque estaban en la misma cadena de
  `await` sin try/catch — ya está separado.

## Cómo correrlo

```
npm install
npm start
```
