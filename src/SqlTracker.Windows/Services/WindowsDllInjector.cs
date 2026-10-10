using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;

namespace SqlTracker.Windows.Services;

public sealed class WindowsDllInjector
{
    public RemoteInjectionHandle Inject(int processId, string dllPath)
    {
        var fullPath = Path.GetFullPath(dllPath);
        if (!File.Exists(fullPath))
        {
            throw new FileNotFoundException("Agent DLL was not found.", fullPath);
        }

        var processHandle = NativeMethods.OpenProcess(NativeMethods.ProcessAllForInjection, false, processId);
        if (processHandle == IntPtr.Zero)
        {
            throw LastWin32Exception("OpenProcess failed");
        }

        IntPtr remoteString = IntPtr.Zero;
        IntPtr threadHandle = IntPtr.Zero;

        try
        {
            var dllBytes = Encoding.Unicode.GetBytes(fullPath + '\0');
            remoteString = NativeMethods.VirtualAllocEx(
                processHandle,
                IntPtr.Zero,
                (nuint)dllBytes.Length,
                NativeMethods.MEM_COMMIT | NativeMethods.MEM_RESERVE,
                NativeMethods.PAGE_READWRITE);

            if (remoteString == IntPtr.Zero)
            {
                throw LastWin32Exception("VirtualAllocEx failed");
            }

            if (!NativeMethods.WriteProcessMemory(processHandle, remoteString, dllBytes, (nuint)dllBytes.Length, out _))
            {
                throw LastWin32Exception("WriteProcessMemory failed");
            }

            var loadLibrary = GetKernel32ProcAddress("LoadLibraryW");
            threadHandle = NativeMethods.CreateRemoteThread(
                processHandle,
                IntPtr.Zero,
                0,
                loadLibrary,
                remoteString,
                0,
                out _);

            if (threadHandle == IntPtr.Zero)
            {
                throw LastWin32Exception("CreateRemoteThread(LoadLibraryW) failed");
            }

            NativeMethods.WaitForSingleObject(threadHandle, NativeMethods.INFINITE);

            if (!NativeMethods.GetExitCodeThread(threadHandle, out var remoteModule) || remoteModule == IntPtr.Zero)
            {
                throw LastWin32Exception("Remote LoadLibraryW failed");
            }

            return new RemoteInjectionHandle(processId, fullPath, remoteModule);
        }
        finally
        {
            if (threadHandle != IntPtr.Zero)
            {
                NativeMethods.CloseHandle(threadHandle);
            }

            if (remoteString != IntPtr.Zero)
            {
                NativeMethods.VirtualFreeEx(processHandle, remoteString, 0, NativeMethods.MEM_RELEASE);
            }

            NativeMethods.CloseHandle(processHandle);
        }
    }

    public void Uninject(RemoteInjectionHandle injectionHandle)
    {
        var processHandle = NativeMethods.OpenProcess(NativeMethods.ProcessAllForInjection, false, injectionHandle.ProcessId);
        if (processHandle == IntPtr.Zero)
        {
            throw LastWin32Exception("OpenProcess failed");
        }

        IntPtr threadHandle = IntPtr.Zero;
        try
        {
            var freeLibrary = GetKernel32ProcAddress("FreeLibrary");
            threadHandle = NativeMethods.CreateRemoteThread(
                processHandle,
                IntPtr.Zero,
                0,
                freeLibrary,
                injectionHandle.RemoteModuleHandle,
                0,
                out _);

            if (threadHandle == IntPtr.Zero)
            {
                throw LastWin32Exception("CreateRemoteThread(FreeLibrary) failed");
            }

            NativeMethods.WaitForSingleObject(threadHandle, NativeMethods.INFINITE);
        }
        finally
        {
            if (threadHandle != IntPtr.Zero)
            {
                NativeMethods.CloseHandle(threadHandle);
            }

            NativeMethods.CloseHandle(processHandle);
        }
    }

    private static IntPtr GetKernel32ProcAddress(string name)
    {
        var kernel32 = NativeMethods.GetModuleHandle("kernel32.dll");
        if (kernel32 == IntPtr.Zero)
        {
            throw LastWin32Exception("GetModuleHandle(kernel32.dll) failed");
        }

        var address = NativeMethods.GetProcAddress(kernel32, name);
        if (address == IntPtr.Zero)
        {
            throw LastWin32Exception($"GetProcAddress({name}) failed");
        }

        return address;
    }

    private static Win32Exception LastWin32Exception(string message)
    {
        return new Win32Exception(Marshal.GetLastWin32Error(), message);
    }

    private static class NativeMethods
    {
        public const uint PROCESS_CREATE_THREAD = 0x0002;
        public const uint PROCESS_QUERY_INFORMATION = 0x0400;
        public const uint PROCESS_VM_OPERATION = 0x0008;
        public const uint PROCESS_VM_WRITE = 0x0020;
        public const uint PROCESS_VM_READ = 0x0010;
        public const uint MEM_COMMIT = 0x1000;
        public const uint MEM_RESERVE = 0x2000;
        public const uint MEM_RELEASE = 0x8000;
        public const uint PAGE_READWRITE = 0x04;
        public const uint INFINITE = 0xFFFFFFFF;

        public const uint ProcessAllForInjection =
            PROCESS_CREATE_THREAD |
            PROCESS_QUERY_INFORMATION |
            PROCESS_VM_OPERATION |
            PROCESS_VM_WRITE |
            PROCESS_VM_READ;

        [DllImport("kernel32.dll", SetLastError = true)]
        public static extern IntPtr OpenProcess(uint dwDesiredAccess, bool bInheritHandle, int dwProcessId);

        [DllImport("kernel32.dll", SetLastError = true)]
        public static extern IntPtr VirtualAllocEx(IntPtr hProcess, IntPtr lpAddress, nuint dwSize, uint flAllocationType, uint flProtect);

        [DllImport("kernel32.dll", SetLastError = true)]
        public static extern bool VirtualFreeEx(IntPtr hProcess, IntPtr lpAddress, nuint dwSize, uint dwFreeType);

        [DllImport("kernel32.dll", SetLastError = true)]
        public static extern bool WriteProcessMemory(IntPtr hProcess, IntPtr lpBaseAddress, byte[] lpBuffer, nuint nSize, out nuint lpNumberOfBytesWritten);

        [DllImport("kernel32.dll", SetLastError = true)]
        public static extern IntPtr CreateRemoteThread(IntPtr hProcess, IntPtr lpThreadAttributes, uint dwStackSize, IntPtr lpStartAddress, IntPtr lpParameter, uint dwCreationFlags, out uint lpThreadId);

        [DllImport("kernel32.dll", SetLastError = true)]
        public static extern uint WaitForSingleObject(IntPtr hHandle, uint dwMilliseconds);

        [DllImport("kernel32.dll", SetLastError = true)]
        public static extern bool GetExitCodeThread(IntPtr hThread, out IntPtr lpExitCode);

        [DllImport("kernel32.dll", SetLastError = true)]
        public static extern bool CloseHandle(IntPtr hObject);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        public static extern IntPtr GetModuleHandle(string lpModuleName);

        [DllImport("kernel32.dll", CharSet = CharSet.Ansi, SetLastError = true)]
        public static extern IntPtr GetProcAddress(IntPtr hModule, string lpProcName);
    }
}
