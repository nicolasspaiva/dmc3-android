# Grafo de chamadas do código recompilado (recomp/ppc): para cada função, as funções que ela chama
# (bl e desvios de cauda; o recompilador emite os dois como "sub_X(ctx, base)") e as chamadas
# ao kernel ("__imp__Nome(ctx, base)").
# Uso: python -I x360_callgraph.py <pasta recomp/ppc> <saida.json>
import glob, json, re, sys


def main():
    ppc_dir, out_path = sys.argv[1:3]
    funcs = {}
    for path in sorted(glob.glob(ppc_dir + '/ppc_recomp.*.cpp')):
        src = open(path).read()
        for m in re.finditer(r'^PPC_FUNC_IMPL\(__imp__sub_([0-9A-F]+)\) \{\n(.*?)^\}', src, re.S | re.M):
            body = m.group(2)
            funcs[int(m.group(1), 16)] = {
                'size': 4 * len(re.findall(r'^\t// ', body, re.M)),
                'calls': sorted({int(t, 16) for t in re.findall(r'\bsub_([0-9A-F]+)\(ctx, base\)', body)}),
                'imports': sorted(set(re.findall(r'__imp__(?!sub_)(\w+)\(ctx, base\)', body))),
                'indirect': body.count('PPC_CALL_INDIRECT_FUNC'),
            }
    json.dump({f'{a:08X}': v for a, v in sorted(funcs.items())}, open(out_path, 'w'), indent=0)
    edges = sum(len(v['calls']) for v in funcs.values())
    print(f'{len(funcs)} funções, {edges} arestas de chamada, '
          f'{sum(1 for v in funcs.values() if v["imports"])} funções chamam o kernel')


if __name__ == '__main__':
    main()
