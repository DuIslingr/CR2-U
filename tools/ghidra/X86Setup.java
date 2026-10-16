// Pre-analysis setup for the Android x86 library: its PIC helper (nop padding then
// mov ebx, [esp]; ret - __x86.get_pc_thunk.bx, stripped) is called by nearly every function,
// and Ghidra's "Non-Returning Functions - Discovered" analyzer takes it for a no-return call,
// cutting every caller short. Turn that analyzer off.
// @category CR2-U
import ghidra.app.script.GhidraScript;

public class X86Setup extends GhidraScript {
    @Override
    protected void run() throws Exception {
        setAnalysisOption(currentProgram, "Non-Returning Functions - Discovered", "false");
        println("X86Setup: non-returning discovery off");
    }
}
