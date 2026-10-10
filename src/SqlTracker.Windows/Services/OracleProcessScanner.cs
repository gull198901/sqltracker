using System.ComponentModel;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;

namespace SqlTracker.Windows.Services;

public sealed class OracleProcessScanner
{
    public IReadOnlyList<OracleProcessInfo> FindOracleProcesses()
    {
        var results = new List<OracleProcessInfo>();

        foreach (var process in Process.GetProcesses())
        {
            using (process)
            {
                if (TryFindOciModule(process.Id, out var ociPath))
                {
                    results.Add(new OracleProcessInfo(
                        process.Id,
                        SafeProcessName(process),
                        TryGetBitness(process.Id),
                        ociPath,
                        TryGetProcessImagePath(process.Id)));
                }
            }
        }

        return results
            .OrderBy(item => item.ProcessName, StringComparer.OrdinalIgnoreCase)
            .ThenBy(item => item.ProcessId)
            .ToArray();
    }

    private static string SafeProcessName(Process process)
    {
        try
        {
            return process.ProcessName;
        }
        catch
        {
            return "<unknown>";
        }
    }

    private static string TryGetProcessImagePath(int processId)
    {
        var handle = NativeMethods.OpenProcess(NativeMethods.PROCESS_QUERY_LIMITED_INFORMATION, false, processId);
        if (handle == IntPtr.Zero)
        {
            return "<unknown path>";
        }

        try
        {
            var builder = new StringBuilder(1024);
            var size = builder.Capacity;
            return NativeMethods.QueryFullProcessImageName(handle, 0, builder, ref size)
                ? builder.ToString()
                : "<unknown path>";
        }
        finally
        {
            NativeMethods.CloseHandle(handle);
        }
    }

    private static bool TryFindOciModule(int processId, out string ociPath)
    {
        ociPath = string.Empty;
        var snapshot = NativeMethods.CreateToolhelp32Snapshot(
            NativeMethods.TH32CS_SNAPMODULE | NativeMethods.TH32CS_SNAPMODULE32,
            (uint)processId);

        if (snapshot == NativeMethods.InvalidHandleValue)
        {
            return false;
        }

        try
        {
            var module = NativeMethods.MODULEENTRY32.Create();
            if (!NativeMethods.Module32First(snapshot, ref module))
            {
                return false;
            }

            do
            {
                if (string.Equals(module.szModule, "oci.dll", StringComparison.OrdinalIgnoreCase))
                {
                    ociPath = module.szExePath;
                    return true;
                }
            }
            while (NativeMethods.Module32Next(snapshot, ref module));
        }
        finally
        {
            NativeMethods.CloseHandle(snapshot);
        }

        return false;
    }

    private static string TryGetBitness(int processId)
    {
        if (!Environment.Is64BitOperatingSystem)
        {
            return "x86";
        }

        var handle = NativeMethods.OpenProcess(NativeMethods.PROCESS_QUERY_LIMITED_INFORMATION, false, processId);
        if (handle == IntPtr.Zero)
        {
            return "unknown";
        }

        try
        {
            return NativeMethods.IsWow64Process(handle, out var isWow64)
                ? isWow64 ? "x86" : "x64"
                : "unknown";
        }
        finally
        {
            NativeMethods.CloseHandle(handle);
        }
    }

    private static class NativeMethods
    {
        public const uint TH32CS_SNAPMODULE = 0x00000008;
        public const uint TH32CS_SNAPMODULE32 = 0x00000010;
        public const uint PROCESS_QUERY_LIMITED_INFORMATION = 0x00001000;
        public static readonly IntPtr InvalidHandleValue = new(-1);

        [DllImport("kernel32.dll", SetLastError = true)]
        public static extern IntPtr CreateToolhelp32Snapshot(uint dwFlags, uint th32ProcessID);

        [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        public static extern bool Module32First(IntPtr hSnapshot, ref MODULEENTRY32 lpme);

        [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        public static extern bool Module32Next(IntPtr hSnapshot, ref MODULEENTRY32 lpme);

        [DllImport("kernel32.dll", SetLastError = true)]
        public static extern bool CloseHandle(IntPtr hObject);

        [DllImport("kernel32.dll", SetLastError = true)]
        public static extern IntPtr OpenProcess(uint dwDesiredAccess, bool bInheritHandle, int dwProcessId);

        [DllImport("kernel32.dll", SetLastError = true)]
        public static extern bool IsWow64Process(IntPtr hProcess, out bool wow64Process);

        [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        public static extern bool QueryFullProcessImageName(IntPtr hProcess, int dwFlags, StringBuilder lpExeName, ref int lpdwSize);

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        public struct MODULEENTRY32
        {
            public uint dwSize;
            public uint th32ModuleID;
            public uint th32ProcessID;
            public uint GlblcntUsage;
            public uint ProccntUsage;
            public IntPtr modBaseAddr;
            public uint modBaseSize;
            public IntPtr hModule;

            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)]
            public string szModule;

            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 260)]
            public string szExePath;

            public static MODULEENTRY32 Create()
            {
                return new MODULEENTRY32
                {
                    dwSize = (uint)Marshal.SizeOf<MODULEENTRY32>(),
                    szModule = string.Empty,
                    szExePath = string.Empty
                };
            }
        }
    }
}
