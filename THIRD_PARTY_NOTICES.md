# Licenças e atribuições

A licença MIT da raiz aplica-se ao código próprio do UFC3MONS. Não relicencia código de terceiros, jogo ou marcas.

- Os fontes derivados de ReXGlue e Xenia, incluindo os arquivos C++ em `sdk-modified/gpu` e `sdk-modified/runtime`, permanecem sob BSD 3-Clause. Consulte `LICENSES/ReXGlue-Xenia-BSD-3-Clause.txt` e os avisos nos próprios arquivos. As modificações nesses arquivos são distribuídas sob a mesma BSD 3-Clause.
- A elevação de cópias da memória compartilhada para antes da passada Vulkan (`ufc_hoist_uploads.h`, `vulkan_command_processor.cpp` e `vsm_r4.cpp`) adapta o patch `patches/rexglue-sdk/0003-vulkan-hoist-shared-memory-uploads.patch` do projeto `nlwstein/skullburglar` (derivado do ReXGlue SDK / Xenia, BSD 3-Clause: Copyright (c) 2022 Ben Vanik / Xenia contributors; Copyright (c) 2026 ReXGlue contributors), com consulta comparativa a `patches/rexglue/0036-perf-hoist-shared-memory-uploads-before-render-pass.patch` de `Dyotson/RecompiledHaloReach` (MIT: Copyright (c) 2026 Dyotson).
- A renderização em resolução nativa dos alvos pequenos (`ufc_draw_resolution_scale_threshold`, correção do sangue que escorria em 2×) adapta o commit `74db632` ("draw_resolution_scale_threshold for host render targets") do projeto Xenia Canary (BSD 3-Clause: Copyright (c) Ben Vanik / Xenia contributors). A barreira entre dois envios seguidos à mesma textura (`ufc_texture_waw_barrier`) segue a correção `c8f214e` do Xenia Edge (BSD 3-Clause).
- A leitura do fim do loop XMA nos bits 14–16 (`ufc_xma_loop_end_hw_layout`) e o `Resume` sem efeito em thread já em execução (`ufc_resume_running_noop`) adaptam os patches `0040` e `0006` de `shaunsingh/pgr4-native` (MIT: Copyright (c) 2026 the pgr4-native authors).
- O tratamento de caminhos longos em `content_manager.cpp` (falha normal em vez de fechar o jogo) adapta o patch `patches/rexglue-sdk/0033-content-manager-no-throw-long-paths.patch` de `nlwstein/skullburglar` (BSD 3-Clause, mesmos detentores acima).
- A checagem de páginas válidas sem a trava global (`ufc_request_range_lockless`) e a fetch constant regravada igual sem invalidar vínculos (`ufc_fetch_unchanged_skip`) adaptam os patches `0031` e `0032` de `Dyotson/RecompiledHaloReach` (MIT: Copyright (c) 2026 Dyotson).
- `app/common/renderdoc_app.h`: Copyright (c) 2015–2026 Baldur Karlsson, MIT; a licença completa está no cabeçalho.
- O shader de readback em `sdk-modified/shaders/copy_delta128.comp` e o cabeçalho correspondente são utilitários do projeto. O cabeçalho coincide byte a byte com o SPIR-V guardado junto do fonte histórico; não foi recompilado nesta revisão.
- Bibliotecas externas referenciadas pelos fontes não são distribuídas integralmente aqui. Cada uma conserva sua licença.

Não se concede licença para UFC Undisputed 3, seu código recompilado, dados, logotipos ou outros materiais do jogo.
