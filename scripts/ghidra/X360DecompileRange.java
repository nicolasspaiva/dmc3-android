// Descompila todas as funções num intervalo de endereços para um arquivo C.
// Uso (headless): -postScript X360DecompileRange.java <saida.c> <hex início> <hex fim>
import java.io.File;
import java.io.PrintWriter;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;

public class X360DecompileRange extends GhidraScript {

	@Override
	public void run() throws Exception {
		String[] args = getScriptArgs();
		Address lo = toAddr(Long.parseLong(args[1], 16));
		Address hi = toAddr(Long.parseLong(args[2], 16));
		DecompInterface dec = new DecompInterface();
		dec.openProgram(currentProgram);
		int ok = 0, bad = 0;
		try (PrintWriter out = new PrintWriter(new File(args[0]), "UTF-8")) {
			FunctionIterator it = currentProgram.getFunctionManager().getFunctions(lo, true);
			while (it.hasNext() && !monitor.isCancelled()) {
				Function f = it.next();
				if (f.getEntryPoint().compareTo(hi) >= 0) {
					break;
				}
				DecompileResults r = dec.decompileFunction(f, 60, monitor);
				if (r.decompileCompleted()) {
					out.printf("// ==== %s @ %s%n%s%n", f.getName(), f.getEntryPoint(), r.getDecompiledFunction().getC());
					ok++;
				} else {
					out.printf("// ==== %s @ %s: falhou (%s)%n%n", f.getName(), f.getEntryPoint(), r.getErrorMessage());
					bad++;
				}
			}
		}
		dec.dispose();
		println("descompiladas: " + ok + ", falhas: " + bad);
	}
}
