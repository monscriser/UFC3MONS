# UFC3MONS — candidata R9 — 21/10/2026

Windows e Linux, Reborn e Original. Junta tudo da R8/R8.1 com as melhorias de desempenho testadas no ROG Ally (Linux) nas builds de teste R8.3–R8.5.

## Correções e melhorias

1. **Fechamento na luta (vibração do controle)** — trava única nas 4 entradas de controle do jogo. No Ally, 124 colisões registradas numa sessão sem nenhum fechamento. (R8)
2. **FPS certo no MangoHud / Afterburner** — a tela só é apresentada quando o jogo entrega um quadro novo. (R8)
3. **Espera ativa do jogo pela GPU com pausa** (`ufc_spin_backoff`). (R8.1)
4. **Thread de tradução para a GPU mais leve:** registradores rápidos + cópia agrupada de constantes (já existiam no plugin, estavam desligados). `UFC_R9_CP_CUTS=false` volta ao comportamento da R8.1. (R8.3)
5. **Atalho na conferência de memória** (Hydro 8.9.1) — plugin de vídeo. (R8.4)
6. **Segunda thread de envio à GPU** (Hydro 9.1.0, adaptado) — plugin de vídeo. O envio de troca de quadro continua síncrono; desliga sozinha se a leitura de resolves (V70) ou os medidores de GPU estiverem ativos. (R8.5)
7. **Linux: thread de áudio a 100% corrigida** — a espera de vários eventos do runtime do Linux arredondava o tempo restante para 0 ms e girava. Corrigido no `librexruntime.so`. No Windows essa espera usa a função nativa e não tinha o problema. (R8.5)

O vsync do console continua ligado (desligar acelerava o jogo).

## Resultado medido (ROG Ally, Linux, Modo Jogo, luta)

| Build | FPS na luta | CPU total |
|---|---|---|
| R8 | 30 (câmera lenta) | ~3,7 núcleos |
| R8.3 | 33–36 | ~3,1 |
| R8.4 | 36–41 | ~3,1 |
| **R8.5 (= R9 Linux)** | **57–60** | **~2,1** |

Quedas curtas ainda acontecem em carregamentos e início de cena.

## Provas

- Cada parte foi montada a partir dos objetos exatos da R7: religar sem mudança reproduziu os binários da R7 (Linux byte a byte; Windows diferindo só no carimbo de data e no separador de pastas nas mensagens).
- 2ª thread de envio: verificação por hash no WSL, 1.422 envios, 0 diferenças; no Ally, 3.859 envios sem falha.
- Thread de áudio: WSL antes ≈1000 ms de CPU/s, depois 99,5% do tempo dormindo.

## Limites

- **A R9 do Windows não foi testada dentro do jogo.** Os dois patches do plugin são os mesmos do Linux, mas a 2ª thread no Windows não teve nem o teste de fumaça.
- A R9 do Linux = binários da R8.5 sem os medidores; os medidores não alteram o jogo, mas esta combinação exata não foi jogada.
- Personagem invisível da Original: não corrigido (investigação em andamento; captura RenderDoc guardada).
