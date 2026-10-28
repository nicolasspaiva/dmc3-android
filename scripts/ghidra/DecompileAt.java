// Create functions at the given addresses (if missing) and print their decompiled C.
// Usage (headless): -postScript DecompileAt.java <out_file> <hexaddr> [<hexaddr> ...]
import java.io.File;
import java.io.PrintWriter;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

public class DecompileAt extends GhidraScript {

	@Override
	public void run() throws Exception {
		String[] args = getScriptArgs();
		DecompInterface dec = new DecompInterface();
		dec.openProgram(currentProgram);
		try (PrintWriter out = new PrintWriter(new File(args[0]), "UTF-8")) {
			for (int i = 1; i < args.length; i++) {
				Address a = toAddr(Long.parseLong(args[i], 16));
				Function f = getFunctionAt(a);
				if (f == null) {
					disassemble(a);
					f = createFunction(a, null);
				}
				if (f == null) {
					out.printf("// %s : could not create function%n%n", a);
					continue;
				}
				DecompileResults r = dec.decompileFunction(f, 60, monitor);
				out.printf("// %s @ %s%n%s%n", f.getName(), a,
					r.decompileCompleted() ? r.getDecompiledFunction().getC() : "decompile failed");
			}
		}
		dec.dispose();
	}
}
