# Dependências e partes que ficam locais

Este pacote não é suficiente para reconstruir os executáveis R9 sozinho.

1. SDK ReXGlue compatível com os cabeçalhos e objetos utilizados nas receitas antigas. Inclui a base Xenia e as dependências SDL, ImGui, Vulkan, fmt/spdlog e outras do SDK.
2. Compiladores C++23: LLVM 19 no Linux e LLVM-MinGW para Windows no ambiente original; CMake/Ninja e Python para as receitas.
3. Código do jogo gerado a partir da cópia do usuário: generated/, ufc3_init, ufc3_register e ufc3_recomp. Todos excluídos.
4. ufc_offline_memory.h: excluído por conter payloads de instruções e textos de origem não estabelecida para publicação. Referências a ele foram mantidas nos fontes sem inventar substitutos.
5. Imagens, ícones e outros assets: excluídos. As receitas antigas podem referenciá-los.
6. Objetos e cabeçalhos adicionais de versões anteriores: as receitas históricas fazem relink incremental, usam PCH e dependem da árvore local anterior. É necessário converter esse processo para uma compilação limpa antes de prometer que terceiros reproduzem a R9.
7. O shader próprio kUfcLosslessSpirv faz parte do utilitário de readback do plugin; não é o código recompilado PPC do jogo. O fonte GLSL foi recuperado em `sdk-modified/shaders/copy_delta128.comp`; o SPIR-V histórico coincide byte a byte com o cabeçalho. Ainda falta validar uma receita portátil de recompilação do shader.

Os snapshots common são uma base de integração, não evidência de que cada um dos objetos herdados da R9 foi recompilado desse exato arquivo. As receitas históricas registram os objetos efetivamente usados. Não houve auditoria completa de reprodução dos objetos herdados nesta separação.

## BETA 1.6

As receitas em `scripts/beta15/` usam o renderer atualizado de `sdk-modified/beta15/gpu/` para a compilação com a correção. Elas ainda dependem do ambiente WSL original, do Clang 19 e dos objetos históricos. Coloque os três headers privados em `dependencias/` ao lado da pasta do repositório. A receita também procura na pasta histórica do renderer.

- `ufc_texture_set_cache_p10.h`
- `ufc_constant_set_cache.h`
- `ufc_hoist_uploads.h`

Para relinkar os executáveis com as mudanças de Carreira, a receita histórica `scripts/beta15/crash-switch/build.py` aceita `UFC3_APP_SOURCE` com o caminho absoluto para `app/common` no WSL. Ela ainda usa os comandos e objetos das árvores P12 e P13.

Nenhum SDK completo, jogo, ISO, save, cache, captura RenderDoc ou executável foi incluído.
