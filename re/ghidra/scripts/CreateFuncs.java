import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.app.cmd.disassemble.DisassembleCommand;
import java.io.*;

// Creates functions at the addresses listed (SEG:OFF per line) in the file given as argument:
// Turbo Pascal routine prologues that the automatic analysis did not reach.
public class CreateFuncs extends GhidraScript {
    public void run() throws Exception {
        BufferedReader r = new BufferedReader(new FileReader(getScriptArgs()[0]));
        String line; int made = 0;
        while ((line = r.readLine()) != null) {
            line = line.trim(); if (line.isEmpty()) continue;
            Address a = currentProgram.getAddressFactory().getAddress(line);
            if (a == null || getFunctionAt(a) != null) continue;
            new DisassembleCommand(a, null, true).applyTo(currentProgram, monitor);
            if (createFunction(a, null) != null) made++;
        }
        r.close();
        println("created " + made + " functions");
        analyzeAll(currentProgram);
    }
}
