// Rename functions from TSV files (address<TAB>name<TAB>reason). Later files win.
// Usage (headless): -postScript ApplyNames.java <names1.tsv> [<names2.tsv> ...]
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.List;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;

public class ApplyNames extends GhidraScript {

	@Override
	public void run() throws Exception {
		int renamed = 0, created = 0, failed = 0;
		for (String path : getScriptArgs()) {
			List<String> lines = Files.readAllLines(Paths.get(path));
			for (String line : lines.subList(1, lines.size())) {
				String[] f = line.split("\t");
				if (f.length < 2) {
					continue;
				}
				Address a = toAddr(Long.parseLong(f[0], 16));
				Function fn = getFunctionAt(a);
				if (fn == null) {
					disassemble(a);
					fn = createFunction(a, null);
					if (fn != null) {
						created++;
					}
				}
				try {
					if (fn != null) {
						fn.setName(f[1].replace("::", "__"), SourceType.USER_DEFINED);
						renamed++;
					}
					else {
						failed++;
					}
				}
				catch (Exception e) {
					failed++;
				}
			}
		}
		println("ApplyNames: renamed=" + renamed + " created=" + created + " failed=" + failed);
	}
}
