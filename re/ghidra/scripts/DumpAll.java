import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;
import java.io.*;

public class DumpAll extends GhidraScript {
    public void run() throws Exception {
        String out = getScriptArgs()[0];
        DecompInterface d = new DecompInterface();
        d.openProgram(currentProgram);
        PrintWriter idx = new PrintWriter(new FileWriter(out + "/functions.tsv"));
        PrintWriter c = new PrintWriter(new FileWriter(out + "/decomp.c"));
        PrintWriter a = new PrintWriter(new FileWriter(out + "/disasm.txt"));
        Listing l = currentProgram.getListing();
        for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
            AddressSetView body = f.getBody();
            idx.println(f.getEntryPoint() + "\t" + f.getName() + "\t" + body.getNumAddresses() + "\t" + f.getCallingConventionName());
            a.println("\n;;;; FUNCTION " + f.getName() + " @ " + f.getEntryPoint());
            InstructionIterator it = l.getInstructions(body, true);
            while (it.hasNext()) { Instruction i = it.next(); a.println(i.getAddress() + "  " + i.toString()); }
            DecompileResults r = d.decompileFunction(f, 120, monitor);
            c.println("\n//// FUNCTION " + f.getName() + " @ " + f.getEntryPoint() + " size " + body.getNumAddresses());
            if (r != null && r.decompileCompleted()) c.println(r.getDecompiledFunction().getC());
            else c.println("// DECOMPILE FAILED: " + (r == null ? "null" : r.getErrorMessage()));
        }
        idx.close(); c.close(); a.close();
    }
}
