#!/usr/bin/env python3
"""BETA 1.5 (fix): Reborn sub_82B50588 - tabela de salto de 32 entradas (cmplwi r29,31; lwzx de 0x82B50690) que o gerador
da Reborn (r9) cortou em 7 casos -> indice 7..31 caia em __builtin_trap (crash 0xC000001D visto em logs de usuario).
A geracao Original (v48) da mesma funcao tem a tabela completa: 7..30 -> loc_82B5077C, 31 -> loc_82B50764.
Correcao exata: no 'default', despacha pelo destino REAL lido da tabela do jogo (ctx.ctr, = o que o bctr do Xbox faz).
So os dois destinos conhecidos da tabela; qualquer outro continua no trap (nada inventado).
Uso: patch_switch.py <ufc3_recomp.229.cpp original> <saida>"""
import sys
src, dst = sys.argv[1], sys.argv[2]
t = open(src, encoding="utf-8").read()
OLD = """	case 6:
		goto loc_82B50758;
	default:
		__builtin_trap(); // Switch case out of range
	}
loc_82B50710:"""
NEW = """	case 6:
		goto loc_82B50758;
	default:
		// UFC3MONS BETA 1.5 fix: table cut to 7 entries by the Reborn generator; dispatch on the real
		// target loaded from the guest table (bctr semantics). Original generation: 7..30 -> 77C, 31 -> 764.
		if (ctx.ctr.u32 == 0x82B5077C) goto loc_82B5077C;
		if (ctx.ctr.u32 == 0x82B50764) goto loc_82B50764;
		__builtin_trap(); // Switch case out of range
	}
loc_82B50710:"""
assert t.count(OLD) == 1, t.count(OLD)
assert t.count("DEFINE_REX_FUNC(sub_82B50588)") == 1
t = t.replace(OLD, NEW)
open(dst, "w", encoding="utf-8", newline="\n").write(t)
print("patch ok")
