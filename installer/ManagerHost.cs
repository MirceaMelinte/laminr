// Setup host (built as FusionSetup.exe). Runs manager.ps1 in an in-process
// Windows PowerShell 5.1 runspace, so the WPF windows get an STA thread and no
// console window appears.
//
// Command line: FusionSetup.exe <installer exe>
//
// Keep to C# 5: build-installer-host.cmd uses the .NET Framework csc.exe.
using System;
using System.IO;
using System.Management.Automation;
using System.Management.Automation.Runspaces;
using System.Reflection;
using System.Threading;
using System.Windows.Forms;

static class ManagerHost
{
    [STAThread]
    static int Main(string[] args)
    {
        try
        {
            RunManager(args);
            return 0;
        }
        catch (Exception e)
        {
            Report(e);
            return 1;
        }
    }

    static void RunManager(string[] args)
    {
        UseWindowsPowerShellModules();
        InitialSessionState session = InitialSessionState.CreateDefault();
        session.ExecutionPolicy = Microsoft.PowerShell.ExecutionPolicy.Bypass;
        using (Runspace runspace = RunspaceFactory.CreateRunspace(session))
        {
            runspace.ApartmentState = ApartmentState.STA;
            runspace.ThreadOptions = PSThreadOptions.UseCurrentThread;
            runspace.Open();
            using (PowerShell shell = PowerShell.Create())
            {
                shell.Runspace = runspace;
                string folder = AppDomain.CurrentDomain.BaseDirectory;
                string installer = args.Length > 0 ? args[0] : Assembly.GetEntryAssembly().Location;
                shell.AddCommand(Path.Combine(folder, "manager.ps1"))
                     .AddParameter("Payload", Path.Combine(folder, "PayloadFusion"))
                     .AddParameter("Installer", installer);
                shell.Invoke();
                // The scripts handle expected errors themselves, so entries in
                // the error stream are not a failure; a failed pipeline is.
                PSInvocationStateInfo state = shell.InvocationStateInfo;
                if (state.State == PSInvocationState.Failed)
                    throw state.Reason ?? new Exception(string.Join(Environment.NewLine, shell.Streams.Error));
            }
        }
    }

    // A parent running PowerShell 7 can pass down a PSModulePath without the
    // Windows PowerShell 5.1 built-in modules, which this runspace needs.
    static void UseWindowsPowerShellModules()
    {
        string builtIn = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System), @"WindowsPowerShell\v1.0\Modules");
        string inherited = Environment.GetEnvironmentVariable("PSModulePath");
        Environment.SetEnvironmentVariable("PSModulePath", string.IsNullOrEmpty(inherited) ? builtIn : builtIn + Path.PathSeparator + inherited);
    }

    static void Report(Exception e)
    {
        try
        {
            MessageBox.Show(e.Message, "Manager setup", MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
        catch
        {
            // Nothing more can be reported; the exit code still signals failure.
        }
    }
}
