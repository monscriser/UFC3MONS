# UFC3MONS — fontes de integração R9

Projeto experimental para executar UFC Undisputed 3 em Windows e Linux com ReXGlue.
Este repositório contém o launcher, código de integração, diagnósticos e fontes modificados do SDK.
Não contém o jogo, executáveis do port nem código recompilado do jogo.

## Conteúdo

- `app/common`: fontes compartilhados preservados da base R7 utilizada pela R9.
- `app/variants`: entradas R9 por plataforma/edição e os demais arquivos específicos utilizados nessa base; prevalecem sobre common.
- `app/fixes`: correção do controle e medidores opcionais (os medidores não integram a entrega R9).
- `app/support`: suporte legado adicional, separado das entradas atuais.
- `sdk-modified`: fontes do plugin GPU Linux R9 e da correção do runtime POSIX. No Windows, a receita histórica recompila apenas vulkan_command_processor e shared_memory, reutilizando outros objetos anteriores.
- `patches`: diferenças R9 contra os snapshots anteriores, não um patch completo sobre qualquer SDK público.
- `scripts/historical`: receitas originais do ambiente de desenvolvimento, com caminhos locais; não são instaladores nem uma compilação portátil.
- `LICENSES`: licença do SDK; o cabeçalho RenderDoc mantém a sua licença MIT dentro do próprio arquivo.

## Estado

Nos registros anteriores, o teste Linux R8.5 que originou a R9 atingiu 57–60 FPS nas lutas do ROG Ally.
A R9 Windows ainda precisa de teste no jogo. O adversário invisível na edição Original continua pendente.
Vsync deve permanecer ligado; o modo escolhido de shaders é síncrono.

## Compilação e limites

Esta é uma separação de fontes para revisão e publicação, não uma entrega autossuficiente que recria a R9.
O processo atual reutiliza objetos e arquivos locais, inclusive código do jogo que não pode acompanhar este pacote.
Consulte `docs/DEPENDENCIAS.md` antes de tentar compilar. Nenhuma compilação ou teste dentro do jogo foi feito para esta separação.

## Atribuições

Os arquivos derivados de ReXGlue/Xenia preservam os avisos e a licença BSD de origem.
`renderdoc_app.h` é de Baldur Karlsson, sob MIT, com licença no cabeçalho.
O código próprio é distribuído sob MIT, por escolha do responsável. Os arquivos derivados de terceiros mantêm suas licenças; consulte `THIRD_PARTY_NOTICES.md`.
Não há afiliação oficial com os titulares do jogo ou das marcas.

## Repositório

Destino escolhido: `monscriser/UFC3MONS`, público. Esta pasta foi preparada localmente; nenhum envio foi realizado.
