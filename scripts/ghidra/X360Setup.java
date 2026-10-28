// Prepara a imagem do Xbox 360 (carregada crua em 0x82000000): desmonta e cria as funções da lista
// (uma por linha, em hex) e dá o nome de kernel às chamadas de importação.
// Uso (headless): -postScript X360Setup.java <functions.txt> [<imports.tsv>]
import java.io.File;
import java.nio.file.Files;
import java.util.List;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;

public class X360Setup extends GhidraScript {

	@Override
	public void run() throws Exception {
		String[] args = getScriptArgs();
		List<String> lines = Files.readAllLines(new File(args[0]).toPath());
		int created = 0, failed = 0;
		for (String line : lines) {
			line = line.trim();
			if (line.isEmpty() || monitor.isCancelled()) {
				continue;
			}
			Address a = toAddr(Long.parseLong(line, 16));
			if (getFunctionAt(a) != null) {
				continue;
			}
			disassemble(a);
			Function f = createFunction(a, "sub_" + line.toUpperCase());
			if (f != null) {
				created++;
			} else {
				failed++;
			}
		}
		println("funções criadas: " + created + ", falhas: " + failed);

		// imports.tsv: "<endereço do thunk>\t<nome>"
		if (args.length > 1) {
			for (String line : Files.readAllLines(new File(args[1]).toPath())) {
				String[] p = line.split("\t");
				if (p.length < 2) {
					continue;
				}
				Address a = toAddr(Long.parseLong(p[0], 16));
				Function f = getFunctionAt(a);
				if (f == null) {
					f = createFunction(a, p[1]);
				}
				if (f != null) {
					f.setName(p[1], SourceType.USER_DEFINED);
				}
			}
		}
	}
}
