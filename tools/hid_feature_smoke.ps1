param(
    [string]$Command = "status",
    [ValidateSet("auto", "xbox", "ds5", "dse", "ns2pro")]
    [string]$Role = "auto",
    [int]$VendorId = 0,
    [int]$ProductId = 0,
    [int]$ReportId = 0x7f,
    [int]$ReportLength = 64,
    [int]$DelayMs = 1000,
    [switch]$NoAccess,
    [switch]$SkipSet,
    [switch]$UseOutputReport,
    [string]$OutputHex = "",
    [ValidateRange(1, 1000)]
    [int]$OutputRepeat = 1,
    [ValidateRange(0, 1000)]
    [int]$OutputIntervalMs = 30
)

$ErrorActionPreference = "Stop"

$source = @"
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;

public static class HidFeatureSmokeNative {
    [StructLayout(LayoutKind.Sequential)]
    public struct GuidStruct {
        public int Data1;
        public short Data2;
        public short Data3;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 8)]
        public byte[] Data4;

        public Guid ToGuid() {
            return new Guid(Data1, Data2, Data3, Data4);
        }
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct SP_DEVICE_INTERFACE_DATA {
        public int cbSize;
        public Guid InterfaceClassGuid;
        public int Flags;
        public IntPtr Reserved;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Auto)]
    public struct SP_DEVICE_INTERFACE_DETAIL_DATA {
        public int cbSize;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 512)]
        public string DevicePath;
    }

    [DllImport("hid.dll")]
    public static extern void HidD_GetHidGuid(out Guid hidGuid);

    [DllImport("hid.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool HidD_SetFeature(IntPtr hidDeviceObject, byte[] reportBuffer, int reportBufferLength);

    [DllImport("hid.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool HidD_GetFeature(IntPtr hidDeviceObject, byte[] reportBuffer, int reportBufferLength);

    [DllImport("hid.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool HidD_GetSerialNumberString(IntPtr hidDeviceObject, StringBuilder buffer, int bufferLength);

    [DllImport("setupapi.dll", SetLastError = true)]
    public static extern IntPtr SetupDiGetClassDevs(ref Guid classGuid, IntPtr enumerator, IntPtr hwndParent, int flags);

    [DllImport("setupapi.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool SetupDiEnumDeviceInterfaces(IntPtr deviceInfoSet, IntPtr deviceInfoData, ref Guid interfaceClassGuid, int memberIndex, ref SP_DEVICE_INTERFACE_DATA deviceInterfaceData);

    [DllImport("setupapi.dll", SetLastError = true, CharSet = CharSet.Auto)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool SetupDiGetDeviceInterfaceDetail(IntPtr deviceInfoSet, ref SP_DEVICE_INTERFACE_DATA deviceInterfaceData, IntPtr deviceInterfaceDetailData, int deviceInterfaceDetailDataSize, out int requiredSize, IntPtr deviceInfoData);

    [DllImport("setupapi.dll", SetLastError = true, CharSet = CharSet.Auto)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool SetupDiGetDeviceInterfaceDetail(IntPtr deviceInfoSet, ref SP_DEVICE_INTERFACE_DATA deviceInterfaceData, ref SP_DEVICE_INTERFACE_DETAIL_DATA deviceInterfaceDetailData, int deviceInterfaceDetailDataSize, out int requiredSize, IntPtr deviceInfoData);

    [DllImport("setupapi.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool SetupDiDestroyDeviceInfoList(IntPtr deviceInfoSet);

    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Auto)]
    public static extern IntPtr CreateFile(string filename, int desiredAccess, int shareMode, IntPtr securityAttributes, int creationDisposition, int flagsAndAttributes, IntPtr templateFile);

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool WriteFile(IntPtr hFile, byte[] lpBuffer, int nNumberOfBytesToWrite, out int lpNumberOfBytesWritten, IntPtr lpOverlapped);

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool CloseHandle(IntPtr handle);

    public const int DIGCF_PRESENT = 0x00000002;
    public const int DIGCF_DEVICEINTERFACE = 0x00000010;
    public const int GENERIC_READ = unchecked((int)0x80000000);
    public const int GENERIC_WRITE = 0x40000000;
    public const int FILE_SHARE_READ = 0x00000001;
    public const int FILE_SHARE_WRITE = 0x00000002;
    public const int OPEN_EXISTING = 3;
    public static readonly IntPtr INVALID_HANDLE_VALUE = new IntPtr(-1);

    public static string ReadSerialNumber(string path) {
        IntPtr handle = CreateFile(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   IntPtr.Zero, OPEN_EXISTING, 0, IntPtr.Zero);
        if (handle == INVALID_HANDLE_VALUE) {
            return null;
        }
        try {
            StringBuilder buffer = new StringBuilder(128);
            return HidD_GetSerialNumberString(handle, buffer,
                                              buffer.Capacity * 2)
                ? buffer.ToString() : null;
        } finally {
            CloseHandle(handle);
        }
    }

    public static string[] EnumerateHidPaths() {
        Guid hidGuid;
        HidD_GetHidGuid(out hidGuid);
        IntPtr set = SetupDiGetClassDevs(ref hidGuid, IntPtr.Zero, IntPtr.Zero, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
        if (set == INVALID_HANDLE_VALUE) {
            throw new Win32Exception(Marshal.GetLastWin32Error());
        }

        List<string> paths = new List<string>();
        try {
            for (int i = 0; ; i++) {
                SP_DEVICE_INTERFACE_DATA ifData = new SP_DEVICE_INTERFACE_DATA();
                ifData.cbSize = Marshal.SizeOf(typeof(SP_DEVICE_INTERFACE_DATA));
                if (!SetupDiEnumDeviceInterfaces(set, IntPtr.Zero, ref hidGuid, i, ref ifData)) {
                    int err = Marshal.GetLastWin32Error();
                    if (err == 259) {
                        break;
                    }
                    throw new Win32Exception(err);
                }

                int required;
                SetupDiGetDeviceInterfaceDetail(set, ref ifData, IntPtr.Zero, 0, out required, IntPtr.Zero);
                SP_DEVICE_INTERFACE_DETAIL_DATA detail = new SP_DEVICE_INTERFACE_DETAIL_DATA();
                detail.cbSize = IntPtr.Size == 8 ? 8 : 5;
                if (!SetupDiGetDeviceInterfaceDetail(set, ref ifData, ref detail, Marshal.SizeOf(typeof(SP_DEVICE_INTERFACE_DETAIL_DATA)), out required, IntPtr.Zero)) {
                    throw new Win32Exception(Marshal.GetLastWin32Error());
                }
                paths.Add(detail.DevicePath);
            }
        } finally {
            SetupDiDestroyDeviceInfoList(set);
        }
        return paths.ToArray();
    }
}
"@

Add-Type -TypeDefinition $source

$mutexName = "Global\SF32LB52_NS2PRO_HID_FEATURE_SMOKE"
$mutex = [Threading.Mutex]::new($false, $mutexName)
$lockTaken = $false

try {
    $lockTaken = $mutex.WaitOne([TimeSpan]::FromSeconds(20))
    if (-not $lockTaken) {
        throw "Timed out waiting for HID feature smoke lock"
    }

    if (($VendorId -eq 0) -xor ($ProductId -eq 0)) {
        throw "VendorId and ProductId must be supplied together"
    }

    $knownRoles = [ordered]@{
        xbox = [pscustomobject]@{ Role = "xbox"; VendorId = 0x045e; ProductId = 0x028e; ReportId = 0x7f }
        ds5 = [pscustomobject]@{ Role = "ds5"; VendorId = 0x054c; ProductId = 0x0ce6; ReportId = 0xf6; Serial = "DualSense HID" }
        dse = [pscustomobject]@{ Role = "dse"; VendorId = 0x054c; ProductId = 0x0df2; ReportId = 0xf6; Serial = "DualSense Edge HID" }
        ns2pro = [pscustomobject]@{ Role = "ns2pro"; VendorId = 0x057e; ProductId = 0x2069; ReportId = 0x7f }
    }
    if ($VendorId -ne 0) {
        $candidates = @([pscustomobject]@{
            Role = "custom"
            VendorId = $VendorId
            ProductId = $ProductId
            ReportId = $ReportId
        })
    } elseif ($Role -eq "auto") {
        $candidates = @($knownRoles.Values)
    } else {
        $candidates = @($knownRoles[$Role])
    }

    $paths = [HidFeatureSmokeNative]::EnumerateHidPaths()
    $match = $null
    foreach ($candidate in $candidates) {
        $vidText = ("vid_{0:x4}" -f $candidate.VendorId)
        $pidText = ("pid_{0:x4}" -f $candidate.ProductId)
        $candidatePaths = @($paths |
            Where-Object { $_.ToLowerInvariant().Contains($vidText) -and $_.ToLowerInvariant().Contains($pidText) })
        $candidatePath = $null
        if ($candidate.Serial) {
            $candidatePath = $candidatePaths |
                Where-Object { [HidFeatureSmokeNative]::ReadSerialNumber($_) -eq $candidate.Serial } |
                Select-Object -First 1
        }
        if (-not $candidatePath) {
            $candidatePath = $candidatePaths | Select-Object -First 1
        }
        if ($candidatePath) {
            $match = [pscustomobject]@{
                Role = $candidate.Role
                Path = $candidatePath
                Vid = $vidText
                Pid = $pidText
                ReportId = $candidate.ReportId
            }
            break
        }
    }

    if (-not $match) {
        $wanted = ($candidates | ForEach-Object { "{0}({1:x4}:{2:x4})" -f $_.Role, $_.VendorId, $_.ProductId }) -join ", "
        throw "No present SF32LB52 manager HID path found for $wanted"
    }

    $path = $match.Path
    if (-not $PSBoundParameters.ContainsKey("ReportId")) {
        $ReportId = $match.ReportId
    }
    Write-Host ("role={0} report=0x{1:x2} path={2}" -f $match.Role, $ReportId, $path)
    $handle = [HidFeatureSmokeNative]::CreateFile(
        $path,
        $(if ($NoAccess) { 0 } else { [HidFeatureSmokeNative]::GENERIC_READ -bor [HidFeatureSmokeNative]::GENERIC_WRITE }),
        [HidFeatureSmokeNative]::FILE_SHARE_READ -bor [HidFeatureSmokeNative]::FILE_SHARE_WRITE,
        [IntPtr]::Zero,
        [HidFeatureSmokeNative]::OPEN_EXISTING,
        0,
        [IntPtr]::Zero)

    if ($handle -eq [HidFeatureSmokeNative]::INVALID_HANDLE_VALUE) {
        throw "CreateFile failed: $([ComponentModel.Win32Exception][Runtime.InteropServices.Marshal]::GetLastWin32Error())"
    }

    try {
        $enc = [Text.Encoding]::ASCII
        $setReport = [byte[]]::new($ReportLength)
        $setReport[0] = [byte]$ReportId
        $payload = $enc.GetBytes("Y7HID1$Command")
        [Array]::Copy($payload, 0, $setReport, 1, [Math]::Min($payload.Length, $ReportLength - 1))

        if ($UseOutputReport) {
            $outReport = [byte[]]::new($ReportLength)
            if ($OutputHex) {
                $cleanHex = $OutputHex -replace '(?i)0x', '' -replace '[^0-9a-fA-F]', ''
                if (($cleanHex.Length % 2) -ne 0) {
                    throw "OutputHex must contain complete bytes"
                }
                $rawLength = [int]($cleanHex.Length / 2)
                if ($rawLength -gt $ReportLength) {
                    throw "OutputHex contains $rawLength bytes, report length is $ReportLength"
                }
                for ($i = 0; $i -lt $rawLength; $i++) {
                    $outReport[$i] = [Convert]::ToByte($cleanHex.Substring($i * 2, 2), 16)
                }
            } else {
                $outReport[0] = 0x02
                [Array]::Copy($payload, 0, $outReport, 1, [Math]::Min($payload.Length, $ReportLength - 1))
            }
            $written = 0
            for ($repeat = 0; $repeat -lt $OutputRepeat; $repeat++) {
                if (-not [HidFeatureSmokeNative]::WriteFile($handle, $outReport, $outReport.Length, [ref]$written, [IntPtr]::Zero)) {
                    throw "WriteFile OUT report failed: $([ComponentModel.Win32Exception][Runtime.InteropServices.Marshal]::GetLastWin32Error())"
                }
                if ($repeat + 1 -lt $OutputRepeat -and $OutputIntervalMs -gt 0) {
                    Start-Sleep -Milliseconds $OutputIntervalMs
                }
            }
            if ($OutputHex) {
                Start-Sleep -Milliseconds $DelayMs
                Write-Host "output_written=$written repeats=$OutputRepeat"
                return
            }
        } elseif (-not $SkipSet) {
            if (-not [HidFeatureSmokeNative]::HidD_SetFeature($handle, $setReport, $setReport.Length)) {
                throw "HidD_SetFeature failed: $([ComponentModel.Win32Exception][Runtime.InteropServices.Marshal]::GetLastWin32Error())"
            }
        }
        Start-Sleep -Milliseconds $DelayMs

        $chunks = @{}
        $total = 0
        for ($i = 0; $i -lt 32; $i++) {
            $getReport = [byte[]]::new($ReportLength)
            $getReport[0] = [byte]$ReportId
            if (-not [HidFeatureSmokeNative]::HidD_GetFeature($handle, $getReport, $getReport.Length)) {
                throw "HidD_GetFeature failed: $([ComponentModel.Win32Exception][Runtime.InteropServices.Marshal]::GetLastWin32Error())"
            }

            $start = 0
            $magic0 = $enc.GetString($getReport, 0, [Math]::Min(6, $getReport.Length))
            $magic1 = if ($getReport.Length -ge 7) { $enc.GetString($getReport, 1, 6) } else { "" }
            if ($magic0 -eq "Y7HRS1") {
                $start = 0
            } elseif ($magic1 -eq "Y7HRS1") {
                $start = 1
            } else {
                $hex = ($getReport[0..([Math]::Min(15, $getReport.Length - 1))] | ForEach-Object { $_.ToString("x2") }) -join " "
                throw "Bad reply magic. first bytes: $hex"
            }

            $total = [int]$getReport[$start + 6] -bor ([int]$getReport[$start + 7] -shl 8)
            $offset = [int]$getReport[$start + 8] -bor ([int]$getReport[$start + 9] -shl 8)
            $len = [int]$getReport[$start + 10]
            $chunk = [byte[]]::new($len)
            if ($len -gt 0) {
                [Array]::Copy($getReport, $start + 11, $chunk, 0, $len)
            }
            $chunks[$offset] = $chunk

            $got = 0
            foreach ($entry in $chunks.GetEnumerator()) {
                $got += $entry.Value.Length
            }
            if ($got -ge $total) {
                break
            }
        }

        $out = [byte[]]::new($total)
        foreach ($offset in ($chunks.Keys | Sort-Object {[int]$_})) {
            $chunk = $chunks[$offset]
            [Array]::Copy($chunk, 0, $out, [int]$offset, $chunk.Length)
        }
        $json = $enc.GetString($out)
        Write-Host "reply=$json"
    } finally {
        [void][HidFeatureSmokeNative]::CloseHandle($handle)
    }
} finally {
    if ($lockTaken) {
        $mutex.ReleaseMutex()
    }
    $mutex.Dispose()
}
