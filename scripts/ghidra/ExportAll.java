// Export functions, strings and decompiled C for the current program.
// Usage (headless): -postScript ExportAll.java <output_dir>
import java.io.File;
import java.io.PrintWriter;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.DataIterator;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;

public class ExportAll extends GhidraScript {

	@Override
	public void run() throws Exception {
		File out = new File(getScriptArgs()[0]);
		out.mkdirs();

		try (PrintWriter fn = new PrintWriter(new File(out, "functions.tsv"), "UTF-8")) {
			fn.println("address\tsize\tname\tcallers\tcallees");
			FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
			while (it.hasNext() && !monitor.isCancelled()) {
				Function f = it.next();
				fn.printf("%s\t%d\t%s\t%d\t%d%n", f.getEntryPoint(), f.getBody().getNumAddresses(), f.getName(),
					f.getCallingFunctions(monitor).size(), f.getCalledFunctions(monitor).size());
			}
		}

		try (PrintWriter st = new PrintWriter(new File(out, "strings.tsv"), "UTF-8")) {
			DataIterator it = currentProgram.getListing().getDefinedData(true);
			while (it.hasNext() && !monitor.isCancelled()) {
				Data d = it.next();
				if (d.hasStringValue()) {
					String v = String.valueOf(d.getValue()).replace("\t", "\\t").replace("\n", "\\n").replace("\r", "\\r");
					st.printf("%s\t%s%n", d.getAddress(), v);
				}
			}
		}

		DecompInterface dec = new DecompInterface();
		dec.openProgram(currentProgram);
		int ok = 0, failed = 0;
		try (PrintWriter c = new PrintWriter(new File(out, "decompiled.c"), "UTF-8")) {
			FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
			while (it.hasNext() && !monitor.isCancelled()) {
				Function f = it.next();
				DecompileResults r = dec.decompileFunction(f, 30, monitor);
				if (r != null && r.decompileCompleted()) {
					c.printf("// %s @ %s%n%s%n", f.getName(), f.getEntryPoint(), r.getDecompiledFunction().getC());
					ok++;
				}
				else {
					c.printf("// %s @ %s : decompile failed%n%n", f.getName(), f.getEntryPoint());
					failed++;
				}
			}
		}
		dec.dispose();
		println("ExportAll: decompiled=" + ok + " failed=" + failed);
	}
}
