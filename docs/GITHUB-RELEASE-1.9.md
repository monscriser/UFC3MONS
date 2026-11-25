# UFC3MONS 1.9

**English** · [Português (Brasil)](#português-brasil) · [Español](#español)

## English

UFC3MONS 1.9 is a fixes and stability release for Windows and Linux (PGO builds). Original game files are not included: you need your own copy of the game.

### What's new

- **Reborn: flyweight fights back to back.** Fixed the game closing when a second flyweight fight was loaded in the same session (pre-fight presentation). Tested on PC with consecutive flyweight Exhibitions, swapped corners and different arenas.
- **Reborn: shirts by organization.** UFC fights now use UFC or neutral shirts and PRIDE fights use PRIDE or neutral shirts. The ONE Championship shirt is no longer used. The choice is made automatically for each fight.
- **Reborn: PRIDE arena wheel.** The PRIDE arena/referee wheel now shows the full list, like the UFC one.
- **Reborn: category titles.** The pre-fight card now shows the flyweight title (Reborn 1.1 had no artwork for it, so it showed BANTAMWEIGHT). With the game in Portuguese, the category titles on the pre-fight card and on the in-fight scoreboard are shown in Portuguese (for example PESO-MOSCA / COMBATE).
- **Reborn in Portuguese: accents restored.** Reborn's menu font had its uppercase accented letters drawn without accents (only Ç survived). The accents are now drawn on those letters using the font's own marks.
- **Portuguese translations:** yearly awards (Fighter, Fight, Knockout and Submission of the Year), Submission of the Night and other labels fixed.
- **System information (Settings):** shows the processor and whether AVX2 is detected. UFC3MONS does not require AVX2. A processor without the required instructions (SSSE3/SSE4.1, plus SSE4.2 for Reborn on Windows) gets a clear message before the game starts instead of a crash.
- **Linux:** fixed a build-up in the GPU copy (readback) tracking that made each lookup more expensive over time.

### Known limitations

- Accented uppercase letters in Reborn's menu font are slightly shorter than the others, to fit the accent in the glyph.
- Steam Deck in-fight FPS was not measured for this release.
- Linux builds were compiled and checked, but this release was played on Windows.

### Requirements

A Vulkan GPU and an x86-64 CPU with SSSE3 and SSE4.1 (SSE4.2 for Reborn on Windows). **AVX2 is not required.**

### A note about the next update

The performance optimizations planned for this release have been postponed as we work on a new stage of UFC3MONS featuring a native Vulkan renderer. This is an ambitious update involving major changes to the graphics architecture, further optimizations, and extensive testing to ensure quality, stability, and compatibility. For this reason, public project updates will be temporarily paused, with no ETA for the next release. Development will continue internally. The next public release will be version 1.13, once it is complete, thoroughly validated, and approved for release.

### SHA-256

| File | SHA-256 |
|---|---|
| UFC3MONS-1.9-PGO-Windows.zip | `a99f65abc2308794407aeb0072b55faaab15074178da79674447bde8c5381608` |
| UFC3MONS-1.9-PGO-Linux-x86_64.AppImage | `f254cd14a55f12a48d86ca74f3a7dc65360952e9cc9d934d5866143574a2bec0` |
| Windows `data/ufc3-launcher-l1.exe` (Reborn) | `c93aa3e00a2144833ddbb2b96e7fc92a2ffd60ef042c98f5e066158255aec24b` |
| Windows `data/ufc3-original-v48.exe` (Original) | `ebc398d7e819289206236cfcc6643f64204bbc4892dc996cbfd257f351220a34` |
| Windows `UFC3.exe` / `Launcher-Config.exe` | `6400c44c98ff1487489ccb97557534d44127788fd8a19b0f73975f50cd3d6f8d` |
| Linux `ufc3-launcher-l1` (Reborn) | `fb130c7e1641c245634264824167cda17e93b1ca53d3f579824f65d1f2e49c74` |
| Linux `ufc3-original-v48` (Original) | `aec42dcf442af89ee0eeecaa19defb9e8f1c3ccf014f92df015ddbffec93e14f` |
| Linux `UFC3` | `fb3faa7111dc2212d48ac97128b6d838331dd899dd3ffb517035cbf48b291b2b` |
| Linux `librexgpu-xenos.so` | `9eadd4d299319f4c8189280d0e19e56f644cea41ab09b809d82ba8017b6c8943` |

## Português (Brasil)

O UFC3MONS 1.9 é uma versão de correções e estabilidade para Windows e Linux (builds PGO). Os arquivos do jogo original não estão incluídos: é preciso ter a sua própria cópia do jogo.

### Novidades

- **Reborn: lutas de peso-mosca seguidas.** Corrigido o fechamento do jogo ao carregar uma segunda luta de peso-mosca na mesma execução (apresentação antes da luta). Testado no PC com Exibições de peso-mosca seguidas, cantos invertidos e arenas diferentes.
- **Reborn: camisas por organização.** As lutas do UFC usam camisas do UFC ou neutras, e as do PRIDE usam camisas do PRIDE ou neutras. A camisa do ONE Championship não é mais usada. A escolha é automática em cada luta.
- **Reborn: roda de arenas do PRIDE.** A roda de arenas e árbitros do PRIDE mostra a lista completa, como a do UFC.
- **Reborn: títulos de categoria.** O card antes da luta agora mostra o título de peso-mosca (o Reborn 1.1 não tinha essa arte e mostrava BANTAMWEIGHT). Com o jogo em português, os títulos de categoria do card antes da luta e do placar durante a luta aparecem em português (por exemplo, PESO-MOSCA / COMBATE).
- **Reborn em português: acentos de volta.** A fonte dos menus do Reborn tinha as maiúsculas acentuadas desenhadas sem acento (só o Ç aparecia). Agora os acentos são desenhados nessas letras usando os sinais da própria fonte.
- **Traduções em português:** prêmios do ano (Lutador, Luta, Nocaute e Finalização do Ano), Finalização da Noite e outros rótulos corrigidos.
- **Informações do sistema (Configurações):** mostra o processador e se o AVX2 foi detectado. O UFC3MONS não exige AVX2. Um processador sem as instruções necessárias (SSSE3/SSE4.1, e SSE4.2 no Reborn para Windows) recebe uma mensagem clara antes de o jogo abrir, em vez de um fechamento.
- **Linux:** corrigido o acúmulo no controle das cópias da GPU (readback) que encarecia cada consulta com o tempo.

### Limitações conhecidas

- As maiúsculas acentuadas da fonte dos menus do Reborn ficam um pouco mais baixas que as demais, para caber o acento.
- O FPS em luta no Steam Deck não foi medido nesta versão.
- As builds Linux foram compiladas e conferidas, mas esta versão foi jogada no Windows.

### Requisitos

Placa de vídeo com Vulkan e processador x86-64 com SSSE3 e SSE4.1 (SSE4.2 no Reborn para Windows). **AVX2 não é necessário.**

### Sobre a próxima atualização

As otimizações de desempenho planejadas para esta versão foram adiadas, pois estamos trabalhando em uma nova etapa do UFC3MONS com renderizador Vulkan nativo. Trata-se de uma atualização ambiciosa, que envolve mudanças profundas na arquitetura gráfica, novas otimizações e uma ampla bateria de testes para garantir qualidade, estabilidade e compatibilidade. Por esse motivo, as atualizações públicas do projeto ficarão temporariamente pausadas, sem prazo definido para a próxima publicação. O desenvolvimento continuará internamente. A próxima versão pública será a 1.13, quando estiver concluída, devidamente validada e aprovada para lançamento.

### SHA-256

| File | SHA-256 |
|---|---|
| UFC3MONS-1.9-PGO-Windows.zip | `a99f65abc2308794407aeb0072b55faaab15074178da79674447bde8c5381608` |
| UFC3MONS-1.9-PGO-Linux-x86_64.AppImage | `f254cd14a55f12a48d86ca74f3a7dc65360952e9cc9d934d5866143574a2bec0` |
| Windows `data/ufc3-launcher-l1.exe` (Reborn) | `c93aa3e00a2144833ddbb2b96e7fc92a2ffd60ef042c98f5e066158255aec24b` |
| Windows `data/ufc3-original-v48.exe` (Original) | `ebc398d7e819289206236cfcc6643f64204bbc4892dc996cbfd257f351220a34` |
| Windows `UFC3.exe` / `Launcher-Config.exe` | `6400c44c98ff1487489ccb97557534d44127788fd8a19b0f73975f50cd3d6f8d` |
| Linux `ufc3-launcher-l1` (Reborn) | `fb130c7e1641c245634264824167cda17e93b1ca53d3f579824f65d1f2e49c74` |
| Linux `ufc3-original-v48` (Original) | `aec42dcf442af89ee0eeecaa19defb9e8f1c3ccf014f92df015ddbffec93e14f` |
| Linux `UFC3` | `fb3faa7111dc2212d48ac97128b6d838331dd899dd3ffb517035cbf48b291b2b` |
| Linux `librexgpu-xenos.so` | `9eadd4d299319f4c8189280d0e19e56f644cea41ab09b809d82ba8017b6c8943` |

## Español

UFC3MONS 1.9 es una versión de correcciones y estabilidad para Windows y Linux (builds PGO). Los archivos del juego original no están incluidos: necesitas tu propia copia del juego.

### Novedades

- **Reborn: peleas de peso mosca seguidas.** Corregido el cierre del juego al cargar una segunda pelea de peso mosca en la misma sesión (presentación previa a la pelea). Probado en PC con Exhibiciones de peso mosca seguidas, esquinas invertidas y arenas diferentes.
- **Reborn: camisetas por organización.** Las peleas de UFC usan camisetas de UFC o neutras, y las de PRIDE usan camisetas de PRIDE o neutras. La camiseta de ONE Championship ya no se usa. La elección es automática en cada pelea.
- **Reborn: rueda de arenas de PRIDE.** La rueda de arenas y árbitros de PRIDE muestra la lista completa, como la de UFC.
- **Reborn: títulos de categoría.** La tarjeta previa a la pelea ahora muestra el título de peso mosca (Reborn 1.1 no tenía ese arte y mostraba BANTAMWEIGHT). Con el juego en portugués, los títulos de categoría de la tarjeta previa y del marcador durante la pelea aparecen en portugués (por ejemplo, PESO-MOSCA / COMBATE).
- **Reborn en portugués: acentos restaurados.** La fuente de los menús de Reborn tenía las mayúsculas acentuadas dibujadas sin acento (solo aparecía la Ç). Ahora los acentos se dibujan en esas letras con los signos de la propia fuente.
- **Traducciones al portugués:** premios del año (Luchador, Pelea, Nocaut y Sumisión del Año), Sumisión de la Noche y otras etiquetas corregidas.
- **Información del sistema (Ajustes):** muestra el procesador y si se detecta AVX2. UFC3MONS no requiere AVX2. Un procesador sin las instrucciones necesarias (SSSE3/SSE4.1, y SSE4.2 en Reborn para Windows) recibe un mensaje claro antes de abrir el juego, en lugar de un cierre.
- **Linux:** corregida la acumulación en el control de copias de la GPU (readback) que encarecía cada consulta con el tiempo.

### Limitaciones conocidas

- Las mayúsculas acentuadas de la fuente de los menús de Reborn quedan un poco más bajas que las demás, para que quepa el acento.
- No se midieron los FPS en pelea en el Steam Deck para esta versión.
- Las builds de Linux se compilaron y verificaron, pero esta versión se jugó en Windows.

### Requisitos

Tarjeta gráfica con Vulkan y procesador x86-64 con SSSE3 y SSE4.1 (SSE4.2 en Reborn para Windows). **AVX2 no es necesario.**

### Sobre la próxima actualización

Las optimizaciones de rendimiento previstas para esta versión se han pospuesto porque estamos trabajando en una nueva etapa de UFC3MONS con un renderizador Vulkan nativo. Se trata de una actualización ambiciosa que implica cambios profundos en la arquitectura gráfica, nuevas optimizaciones y una amplia batería de pruebas para garantizar la calidad, la estabilidad y la compatibilidad. Por este motivo, las actualizaciones públicas del proyecto quedarán temporalmente en pausa, sin un plazo definido para la próxima publicación. El desarrollo continuará de forma interna. La próxima versión pública será la 1.13, cuando esté terminada, debidamente validada y aprobada para su lanzamiento.

### SHA-256

| File | SHA-256 |
|---|---|
| UFC3MONS-1.9-PGO-Windows.zip | `a99f65abc2308794407aeb0072b55faaab15074178da79674447bde8c5381608` |
| UFC3MONS-1.9-PGO-Linux-x86_64.AppImage | `f254cd14a55f12a48d86ca74f3a7dc65360952e9cc9d934d5866143574a2bec0` |
| Windows `data/ufc3-launcher-l1.exe` (Reborn) | `c93aa3e00a2144833ddbb2b96e7fc92a2ffd60ef042c98f5e066158255aec24b` |
| Windows `data/ufc3-original-v48.exe` (Original) | `ebc398d7e819289206236cfcc6643f64204bbc4892dc996cbfd257f351220a34` |
| Windows `UFC3.exe` / `Launcher-Config.exe` | `6400c44c98ff1487489ccb97557534d44127788fd8a19b0f73975f50cd3d6f8d` |
| Linux `ufc3-launcher-l1` (Reborn) | `fb130c7e1641c245634264824167cda17e93b1ca53d3f579824f65d1f2e49c74` |
| Linux `ufc3-original-v48` (Original) | `aec42dcf442af89ee0eeecaa19defb9e8f1c3ccf014f92df015ddbffec93e14f` |
| Linux `UFC3` | `fb3faa7111dc2212d48ac97128b6d838331dd899dd3ffb517035cbf48b291b2b` |
| Linux `librexgpu-xenos.so` | `9eadd4d299319f4c8189280d0e19e56f644cea41ab09b809d82ba8017b6c8943` |
