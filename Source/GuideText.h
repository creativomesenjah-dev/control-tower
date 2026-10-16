// GuideText.h — texto de la ventana "GUÍA" (mapas de la FLX4, nanoPAD2 y teclado).
#pragma once

namespace ct
{

static const char* const kGuideText = u8R"(CONTROL TOWER — GUÍA DE CONTROLES

DDJ-FLX4
────────────────────────────────────────────────────────────
BROWSE girar ........ recorrer carpetas / temas (SHIFT: de a 10)
BROWSE apretar ...... cambiar entre carpetas y temas
LOAD 1 / LOAD 2 ..... cargar el tema elegido (no si el deck suena)
PLAY / CUE .......... play-pausa / cue estilo CDJ
SHIFT + CUE ......... volver al principio
JOG ................. tocado: scratch · borde: empujar pitch
SHIFT + JOG ......... búsqueda rápida
TEMPO ............... pitch (SHIFT + BEAT SYNC: rango 8/16/50%)
BEAT SYNC ........... TAP TEMPO del echo
LOOP IN/OUT/RELOOP .. loop manual · CUE/LOOP CALL ◀ ▶: mitad / doble
TRIM HI MID LOW ..... EQ aislador del canal (al mínimo = kill)
CFX ................. filtro del canal
CUE de canal ........ pre-escucha · HEADPHONES MIX: cue / master

SECCIÓN BEAT FX = TAPE ECHO
  LEVEL/DEPTH ....... repeticiones
  ON/OFF ............ tocar: envío fijo al echo · mantener: THROW
  SHIFT + ON/OFF .... lo mismo para el SPRING
  SELECT ............ kill del echo
  BEAT ◀ ▶ .......... división del echo
  CH 1 / CH 2 ....... de qué canal sale el envío (al medio: los dos)

PADS (el modo se cambia con los botones de modo, como siempre)
  HOT CUE ........... hot cues 1-8 (SHIFT + pad: borrar)
  PAD FX1 ........... 1 THROW ECHO · 2 THROW SPRING · 3 FREEZE · 4 KILL ECHO
                      5 TIEMPO − (mantener) · 6 TIEMPO + (mantener)
                      7 ENVÍO FIJO ECHO · 8 ENVÍO FIJO SPRING
  BEAT JUMP ......... AISLADOR (divisor de frecuencias):
                      1-5 kill de cada banda · 6 FLAT · 7 SUB BOOST · 8 cambiar vías 3/4/5
  SAMPLER ........... samples 1-8 (izq.) y 9-16 (der.) · SHIFT + pad: parar
  KEYBOARD (SHIFT) .. 1 dos tonos · 2 láser · 3 ondulante · 4 caída · 5 sub drop
                      6 throw echo · 7 throw spring · 8 freeze
  BEAT LOOP (SHIFT) . tiempo del echo: 1/16 1/8 1/8· 1/4 1/4· 1/2 1/4T · 8 TAP
  PAD FX2 / KEY SHIFT  libres: asignalos en MAPEO MIDI

KORG nanoPAD2 (cambiá de escena con el botón SCENE)
────────────────────────────────────────────────────────────
PAD XY .............. X = tiempo del echo · Y = repeticiones
                      con una sirena sonando: X = tono · Y = velocidad
                      al soltar el dedo, los valores quedan donde estaban

ESCENA 1 · DUB / ECHO, igual que Dub Echo (notas 36 a 51, en este orden)
  THROW · TAP · KILL · FREEZE · ENVÍO FIJO
  1/8 · 1/8· · 1/4 · 1/4· · 1/4T · TIEMPO − · TIEMPO + (mantener)
  sirenas: DOS TONOS · LÁSER · ONDULANTE · CAÍDA

ESCENA 2 · SPRING, AISLADOR Y ENVÍOS (notas 52 a 67)
  THROW SPRING · SPRING FIJO · THROW ECHO+SPRING · SUB DROP
  KILL 1 · KILL 2 · KILL 3 · KILL 4 · KILL 5 · FLAT · SUB BOOST · VÍAS 3/4/5
  ENVÍO DESDE CANAL 1 · CANAL 2 · LOS DOS · PARAR SAMPLES

ESCENA 3 · SAMPLES 1 a 16 (notas 68 a 83)

ESCENA 4 · DECKS (notas 84 a 99)
  PLAY 1 · CUE 1 · PLAY 2 · CUE 2
  HOT CUE 1-4 del deck 1 · HOT CUE 1-4 del deck 2
  RELOOP 1 · RELOOP 2 · LOAD 1 · LOAD 2

Si tu nanoPAD2 manda otras notas, cualquier pad se reasigna en MAPEO MIDI
(lo que aprendas ahí tiene prioridad sobre el mapeo de fábrica).
Si el eje X no responde: apagá el botón TOUCH SCALE del nanoPAD2.

TECLADO
────────────────────────────────────────────────────────────
Z X C V B (mantener) ... sirenas 1 a 5
E / R (mantener) ....... throw al echo / al spring
F (mantener) ........... freeze del echo
K / T .................. kill del echo / tap tempo
1 a 7 .................. divisiones del echo
Q / P .................. play deck 1 / deck 2
flechas ................ biblioteca · [ ] cargar en deck 1 / 2
)";

} // namespace ct
