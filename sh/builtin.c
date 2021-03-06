/**
 * @file /sh/builtin.c
 *
 * Yori shell built in function handler
 *
 * Copyright (c) 2017 Malcolm J. Smith
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "yori.h"

/**
 Invoke a different program to complete executing the command.  This may be
 Powershell for its scripts, YS for its scripts, etc.

 @param ExecContext Pointer to the exec context for a command that is known
        to be implemented by an external program.

 @param ExtraArgCount The number of extra arguments that should be inserted
        into the beginning of the ExecContext, followed by a set of NULL
        terminated strings to insert into the ExecContext.

 @return ExitCode from process, or nonzero on failure.
 */
DWORD
YoriShBuckPass (
    __in PYORI_LIBSH_SINGLE_EXEC_CONTEXT ExecContext,
    __in DWORD ExtraArgCount,
    ...
    )
{
    YORI_LIBSH_CMD_CONTEXT OldCmdContext;
    DWORD ExitCode = 1;
    DWORD count;
    BOOL ExecAsBuiltin = FALSE;
    va_list marker;

    memcpy(&OldCmdContext, &ExecContext->CmdToExec, sizeof(YORI_LIBSH_CMD_CONTEXT));

    ExecContext->CmdToExec.ArgC += ExtraArgCount;
    ExecContext->CmdToExec.MemoryToFree = YoriLibReferencedMalloc(ExecContext->CmdToExec.ArgC * (sizeof(YORI_STRING) + sizeof(YORI_LIBSH_ARG_CONTEXT)));
    if (ExecContext->CmdToExec.MemoryToFree == NULL) {
        memcpy(&ExecContext->CmdToExec, &OldCmdContext, sizeof(YORI_LIBSH_CMD_CONTEXT));
        return ExitCode;
    }

    ExecContext->CmdToExec.ArgV = ExecContext->CmdToExec.MemoryToFree;

    ExecContext->CmdToExec.ArgContexts = (PYORI_LIBSH_ARG_CONTEXT)YoriLibAddToPointer(ExecContext->CmdToExec.ArgV, sizeof(YORI_STRING) * ExecContext->CmdToExec.ArgC);
    ZeroMemory(ExecContext->CmdToExec.ArgContexts, sizeof(YORI_LIBSH_ARG_CONTEXT) * ExecContext->CmdToExec.ArgC);

    va_start(marker, ExtraArgCount);
    for (count = 0; count < ExtraArgCount; count++) {
        LPTSTR NewArg = (LPTSTR)va_arg(marker, LPTSTR);
        YORI_STRING YsNewArg;
        YORI_STRING FoundInPath;

        YoriLibInitEmptyString(&ExecContext->CmdToExec.ArgV[count]);
        YoriLibConstantString(&YsNewArg, NewArg);

        //
        //  Search for the first extra argument in the path.  If we find it,
        //  execute as a program; if not, execute as a builtin.
        //

        if (count == 0) {
            YoriLibInitEmptyString(&FoundInPath);
            if (YoriLibLocateExecutableInPath(&YsNewArg, NULL, NULL, &FoundInPath) && FoundInPath.LengthInChars > 0) {
                memcpy(&ExecContext->CmdToExec.ArgV[0], &FoundInPath, sizeof(YORI_STRING));
                ASSERT(YoriLibIsStringNullTerminated(&ExecContext->CmdToExec.ArgV[0]));
                YoriLibInitEmptyString(&FoundInPath);
            } else {
                ExecAsBuiltin = TRUE;
                YoriLibConstantString(&ExecContext->CmdToExec.ArgV[count], NewArg);
            }

            YoriLibFreeStringContents(&FoundInPath);
        } else {
            YoriLibConstantString(&ExecContext->CmdToExec.ArgV[count], NewArg);
        }
    }
    va_end(marker);

    for (count = 0; count < OldCmdContext.ArgC; count++) {
        YoriLibShCopyArg(&OldCmdContext, count, &ExecContext->CmdToExec, count + ExtraArgCount);
    }

    YoriLibShCheckIfArgNeedsQuotes(&ExecContext->CmdToExec, 0);

    if (ExecAsBuiltin) {
        ExitCode = YoriShBuiltIn(ExecContext);
    } else {
        ExecContext->IncludeEscapesAsLiteral = TRUE;
        ExitCode = YoriShExecuteSingleProgram(ExecContext);
    }

    YoriLibShFreeCmdContext(&ExecContext->CmdToExec);
    memcpy(&ExecContext->CmdToExec, &OldCmdContext, sizeof(YORI_LIBSH_CMD_CONTEXT));

    return ExitCode;
}

/**
 Invoke CMD to execute a script.  This is different to regular child
 processes because CMD wants all arguments to be enclosed in quotes, in
 addition to quotes around individual arguments, as in:
 C:\\Windows\\System32\\Cmd.exe /c ""C:\\Path To\\Foo.cmd" "Arg To Foo""

 @param ExecContext Pointer to the exec context for a command that is known
        to be implemented by CMD.

 @return ExitCode from CMD, or nonzero on failure.
 */
DWORD
YoriShBuckPassToCmd (
    __in PYORI_LIBSH_SINGLE_EXEC_CONTEXT ExecContext
    )
{
    YORI_LIBSH_CMD_CONTEXT OldCmdContext;
    YORI_STRING CmdLine;
    DWORD ExitCode = EXIT_SUCCESS;

    memcpy(&OldCmdContext, &ExecContext->CmdToExec, sizeof(YORI_LIBSH_CMD_CONTEXT));
    YoriLibInitEmptyString(&CmdLine);
    if (!YoriLibShBuildCmdlineFromCmdContext(&OldCmdContext, &CmdLine, FALSE, NULL, NULL)) {
        return EXIT_FAILURE;
    }

    if (!YoriLibShBuildCmdContextForCmdBuckPass(&ExecContext->CmdToExec, &CmdLine)) {
        YoriLibFreeStringContents(&CmdLine);
        return EXIT_FAILURE;
    }

    YoriLibFreeStringContents(&CmdLine);
    ExecContext->IncludeEscapesAsLiteral = TRUE;
    ExitCode = YoriShExecuteSingleProgram(ExecContext);

    YoriLibShFreeCmdContext(&ExecContext->CmdToExec);
    memcpy(&ExecContext->CmdToExec, &OldCmdContext, sizeof(YORI_LIBSH_CMD_CONTEXT));

    return ExitCode;
}

/**
 Call a builtin function.  This may be in a DLL or part of the main executable,
 but it is executed synchronously via a call rather than a CreateProcess.

 @param Fn The function associated with the builtin operation to call.

 @param ExecContext The context surrounding this particular function.

 @return ExitCode, typically zero for success, nonzero for failure.
 */
int
YoriShExecuteInProc(
    __in PYORI_CMD_BUILTIN Fn,
    __in PYORI_LIBSH_SINGLE_EXEC_CONTEXT ExecContext
    )
{
    YORI_LIBSH_PREVIOUS_REDIRECT_CONTEXT PreviousRedirectContext;
    BOOLEAN WasPipe = FALSE;
    PYORI_LIBSH_CMD_CONTEXT OriginalCmdContext = &ExecContext->CmdToExec;
    PYORI_LIBSH_CMD_CONTEXT SavedEscapedCmdContext;
    YORI_LIBSH_CMD_CONTEXT NoEscapesCmdContext;
    YORI_STRING CmdLine;
    PYORI_STRING ArgV;
    DWORD ArgC;
    DWORD Count;
    DWORD ExitCode = 0;

    if (!YoriLibShRemoveEscapesFromCmdContext(OriginalCmdContext, &NoEscapesCmdContext)) {
        return ERROR_OUTOFMEMORY;
    }

    //
    //  We execute builtins on a single thread due to the amount of
    //  process wide state that could get messed up if we don't (eg.
    //  stdout.)  Unfortunately this means we can't natively implement
    //  things like pipe from builtins, because the builtin has to
    //  finish before the next process can start.  So if a pipe is
    //  requested, convert it into a buffer, and let the process
    //  finish.
    //

    if (ExecContext->StdOutType == StdOutTypePipe) {
        WasPipe = TRUE;
        ExecContext->StdOutType = StdOutTypeBuffer;
    }

    //
    //  Check if an argument isn't quoted but requires quotes.  This implies
    //  something happened outside the user's immediate control, such as
    //  environment variable expansion.  When this occurs, reprocess the
    //  command back to a string form and recompose into ArgC/ArgV using the
    //  same routines as would occur for an external process.
    //

    ArgC = NoEscapesCmdContext.ArgC;
    ArgV = NoEscapesCmdContext.ArgV;

    for (Count = 0; Count < NoEscapesCmdContext.ArgC; Count++) {
        ASSERT(YoriLibIsStringNullTerminated(&NoEscapesCmdContext.ArgV[Count]));
        if (!NoEscapesCmdContext.ArgContexts[Count].Quoted &&
            YoriLibCheckIfArgNeedsQuotes(&NoEscapesCmdContext.ArgV[Count]) &&
            ArgV == NoEscapesCmdContext.ArgV) {

            YoriLibInitEmptyString(&CmdLine);
            if (!YoriLibShBuildCmdlineFromCmdContext(&NoEscapesCmdContext, &CmdLine, TRUE, NULL, NULL)) {
                YoriLibShFreeCmdContext(&NoEscapesCmdContext);
                return ERROR_OUTOFMEMORY;
            }

            ASSERT(YoriLibIsStringNullTerminated(&CmdLine));
            ArgV = YoriLibCmdlineToArgcArgv(CmdLine.StartOfString, (DWORD)-1, &ArgC);
            YoriLibFreeStringContents(&CmdLine);

            if (ArgV == NULL) {
                YoriLibShFreeCmdContext(&NoEscapesCmdContext);
                return ERROR_OUTOFMEMORY;
            }
        }
    }

    ExitCode = YoriLibShInitializeRedirection(ExecContext, TRUE, &PreviousRedirectContext);
    if (ExitCode != ERROR_SUCCESS) {
        if (ArgV != NoEscapesCmdContext.ArgV) {
            for (Count = 0; Count < ArgC; Count++) {
                YoriLibFreeStringContents(&ArgV[Count]);
            }
            YoriLibDereference(ArgV);
        }
        YoriLibShFreeCmdContext(&NoEscapesCmdContext);

        return ExitCode;
    }

    //
    //  Unlike external processes, builtins need to start buffering
    //  before they start to ensure that output during execution has
    //  somewhere to go.
    //

    if (ExecContext->StdOutType == StdOutTypeBuffer) {
        if (ExecContext->StdOut.Buffer.ProcessBuffers != NULL) {
            if (YoriLibShAppendToExistingProcessBuffer(ExecContext)) {
                ExecContext->StdOut.Buffer.PipeFromProcess = NULL;
            } else {
                ExecContext->StdOut.Buffer.ProcessBuffers = NULL;
            }
        } else {
            if (YoriLibShCreateNewProcessBuffer(ExecContext)) {
                ExecContext->StdOut.Buffer.PipeFromProcess = NULL;
            }
        }
    }

    SavedEscapedCmdContext = YoriShGlobal.EscapedCmdContext;
    YoriShGlobal.EscapedCmdContext = OriginalCmdContext;
    YoriShGlobal.RecursionDepth++;
    ExitCode = Fn(ArgC, ArgV);
    YoriShGlobal.RecursionDepth--;
    YoriShGlobal.EscapedCmdContext = SavedEscapedCmdContext;
    YoriLibShRevertRedirection(&PreviousRedirectContext);

    if (WasPipe) {
        YoriLibShForwardProcessBufferToNextProcess(ExecContext);
    } else {

        //
        //  Once the process has completed, if it's outputting to
        //  buffers, wait for the buffers to contain final data.
        //

        if (ExecContext->StdOutType == StdOutTypeBuffer &&
            ExecContext->StdOut.Buffer.ProcessBuffers != NULL)  {

            YoriLibShWaitForProcessBufferToFinalize(ExecContext->StdOut.Buffer.ProcessBuffers);
        }

        if (ExecContext->StdErrType == StdErrTypeBuffer &&
            ExecContext->StdErr.Buffer.ProcessBuffers != NULL) {

            YoriLibShWaitForProcessBufferToFinalize(ExecContext->StdErr.Buffer.ProcessBuffers);
        }
    }

    if (ArgV != NoEscapesCmdContext.ArgV) {
        for (Count = 0; Count < ArgC; Count++) {
            YoriLibFreeStringContents(&ArgV[Count]);
        }
        YoriLibDereference(ArgV);
    }
    YoriLibShFreeCmdContext(&NoEscapesCmdContext);

    return ExitCode;
}

/**
 Execute a command contained in a DLL file.

 @param ModuleFileName Pointer to the full path to the DLL file.

 @param ExecContext Specifies all of the arguments that should be passed to
        the module.

 @param ExitCode On successful completion, updated to point to the exit code
        from the module.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
__success(return)
BOOL
YoriShExecuteNamedModuleInProc(
    __in LPTSTR ModuleFileName,
    __in PYORI_LIBSH_SINGLE_EXEC_CONTEXT ExecContext,
    __out PDWORD ExitCode
    )
{
    PYORI_CMD_BUILTIN Main;
    PYORI_LIBSH_LOADED_MODULE Module;

    Module = YoriLibShLoadDll(ModuleFileName);
    if (Module == NULL) {
        return FALSE;
    }

    Main = (PYORI_CMD_BUILTIN)GetProcAddress(Module->ModuleHandle, "YoriMain");
    if (Main == NULL) {
        YoriLibShReleaseDll(Module);
        return FALSE;
    }

    if (Main) {
        PYORI_LIBSH_LOADED_MODULE PreviousModule;

        PreviousModule = YoriLibShGetActiveModule();
        YoriLibShSetActiveModule(Module);
        *ExitCode = YoriShExecuteInProc(Main, ExecContext);
        YoriLibShSetActiveModule(PreviousModule);
    }

    YoriLibShReleaseDll(Module);
    return TRUE;
}


/**
 Execute a function if we can't find it in the PATH.  Because Yori looks
 for programs in the path first, this function acts as a "last chance" to
 see if there's an internal implementation before we give up and fail.

 @param ExecContext Pointer to the exec context for this program.

 @return ExitCode, typically zero for success or nonzero on failure.
 */
DWORD
YoriShBuiltIn (
    __in PYORI_LIBSH_SINGLE_EXEC_CONTEXT ExecContext
    )
{
    DWORD ExitCode = 1;
    PYORI_LIBSH_CMD_CONTEXT CmdContext = &ExecContext->CmdToExec;
    PYORI_CMD_BUILTIN BuiltInCmd = NULL;
    PYORI_LIBSH_BUILTIN_CALLBACK CallbackEntry = NULL;


    CallbackEntry = YoriLibShLookupBuiltinByName(&CmdContext->ArgV[0]);
    if (CallbackEntry != NULL) {
        BuiltInCmd = CallbackEntry->BuiltInFn;
    }

    if (BuiltInCmd) {
        PYORI_LIBSH_LOADED_MODULE PreviousModule;
        PYORI_LIBSH_LOADED_MODULE HostingModule = NULL;

        ASSERT(CallbackEntry != NULL);

        //
        //  If the function is in a module, reference the DLL to keep it alive
        //  until it returns.
        //

        if (CallbackEntry != NULL && CallbackEntry->ReferencedModule != NULL) {
            HostingModule = CallbackEntry->ReferencedModule;
            YoriLibShReferenceDll(HostingModule);
        }

        //
        //  Indicate which module is currently executing, and execute from it.
        //

        PreviousModule = YoriLibShGetActiveModule();
        YoriLibShSetActiveModule(HostingModule);
        ExitCode = YoriShExecuteInProc(BuiltInCmd, ExecContext);
        ASSERT(YoriLibShGetActiveModule() == HostingModule);
        YoriLibShSetActiveModule(PreviousModule);

        if (HostingModule != NULL) {
            YoriLibShReleaseDll(HostingModule);
        }
    } else {
        YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("Unrecognized command: %y\n"), &CmdContext->ArgV[0]);
        if (ExecContext->StdOutType == StdOutTypePipe &&
            ExecContext->NextProgram != NULL &&
            ExecContext->NextProgram->StdInType == StdInTypePipe) {

            ExecContext->NextProgramType = NextProgramExecNever;
        }
    }

    return ExitCode;
}

/**
 Execute a command that is built in to the shell.  This can be used by in
 process extension modules.

 @param Expression The expression to execute.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
__success(return)
BOOL
YoriShExecuteBuiltinString(
    __in PYORI_STRING Expression
    )
{
    YORI_LIBSH_EXEC_PLAN ExecPlan;
    YORI_LIBSH_CMD_CONTEXT CmdContext;
    PYORI_LIBSH_SINGLE_EXEC_CONTEXT ExecContext;

    //
    //  Parse the expression we're trying to execute.
    //

    if (!YoriLibShParseCmdlineToCmdContext(Expression, 0, &CmdContext)) {
        YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("Parse error\n"));
        return FALSE;
    }

    if (CmdContext.ArgC == 0) {
        YoriLibShFreeCmdContext(&CmdContext);
        return FALSE;
    }

    if (!YoriShExpandEnvironmentInCmdContext(&CmdContext)) {
        YoriLibShFreeCmdContext(&CmdContext);
        return FALSE;
    }

    if (!YoriLibShParseCmdContextToExecPlan(&CmdContext, &ExecPlan, NULL, NULL, NULL, NULL)) {
        YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("Parse error\n"));
        YoriLibShFreeCmdContext(&CmdContext);
        return FALSE;
    }

    ExecContext = ExecPlan.FirstCmd;

    if (ExecContext->NextProgram != NULL) {
        YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("Attempt to invoke multi component expression as a builtin\n"));
        YoriLibShFreeExecPlan(&ExecPlan);
        YoriLibShFreeCmdContext(&CmdContext);
        return FALSE;
    }

    YoriShGlobal.ErrorLevel = YoriShBuiltIn(ExecContext);

    YoriLibShFreeExecPlan(&ExecPlan);
    YoriLibShFreeCmdContext(&CmdContext);

    return TRUE;
}

// vim:sw=4:ts=4:et:
