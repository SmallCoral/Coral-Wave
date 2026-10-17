using System;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;

// Full-duplex test using only the named Coral-Wave device. Default routing is unchanged.
public static class CoralWaveUsbTest
{
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    struct OutCaps {
        public ushort manufacturer, product;
        public uint version;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string name;
        public uint formats;
        public ushort channels, reserved;
        public uint support;
    }
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    struct InCaps {
        public ushort manufacturer, product;
        public uint version;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string name;
        public uint formats;
        public ushort channels, reserved;
    }
    [StructLayout(LayoutKind.Sequential, Pack = 2)]
    struct Format {
        public ushort tag, channels;
        public uint rate, bytesPerSecond;
        public ushort blockAlign, bits, extra;
    }
    [StructLayout(LayoutKind.Sequential)]
    struct Header {
        public IntPtr data;
        public uint length, recorded;
        public UIntPtr user;
        public uint flags, loops;
        public IntPtr next;
        public UIntPtr reserved;
    }
    public sealed class Result {
        public string PlaybackDevice, CaptureDevice;
        public uint PlaybackBytes, CaptureBytes;
        public bool CaptureIsSilence;
        public int CapturePeak;
        public int CaptureNonzeroSamples;
        public double CaptureRms;
        public int[] PeaksPerSecond;
        public double[] RmsPerSecond;
        public string CaptureFile;
        public int Seconds;
        public int SampleRate = 48000, Bits = 16;
    }
    [DllImport("winmm.dll")] static extern uint waveOutGetNumDevs();
    [DllImport("winmm.dll")] static extern uint waveInGetNumDevs();
    [DllImport("winmm.dll", CharSet = CharSet.Unicode)] static extern uint waveOutGetDevCapsW(UIntPtr id, out OutCaps caps, uint size);
    [DllImport("winmm.dll", CharSet = CharSet.Unicode)] static extern uint waveInGetDevCapsW(UIntPtr id, out InCaps caps, uint size);
    [DllImport("winmm.dll")] static extern uint waveOutOpen(out IntPtr handle, uint id, ref Format format, IntPtr callback, IntPtr instance, uint flags);
    [DllImport("winmm.dll")] static extern uint waveInOpen(out IntPtr handle, uint id, ref Format format, IntPtr callback, IntPtr instance, uint flags);
    [DllImport("winmm.dll")] static extern uint waveOutPrepareHeader(IntPtr handle, IntPtr header, uint size);
    [DllImport("winmm.dll")] static extern uint waveInPrepareHeader(IntPtr handle, IntPtr header, uint size);
    [DllImport("winmm.dll")] static extern uint waveOutWrite(IntPtr handle, IntPtr header, uint size);
    [DllImport("winmm.dll")] static extern uint waveInAddBuffer(IntPtr handle, IntPtr header, uint size);
    [DllImport("winmm.dll")] static extern uint waveInStart(IntPtr handle);
    [DllImport("winmm.dll")] static extern uint waveOutReset(IntPtr handle);
    [DllImport("winmm.dll")] static extern uint waveInReset(IntPtr handle);
    [DllImport("winmm.dll")] static extern uint waveOutUnprepareHeader(IntPtr handle, IntPtr header, uint size);
    [DllImport("winmm.dll")] static extern uint waveInUnprepareHeader(IntPtr handle, IntPtr header, uint size);
    [DllImport("winmm.dll")] static extern uint waveOutClose(IntPtr handle);
    [DllImport("winmm.dll")] static extern uint waveInClose(IntPtr handle);

    static void Check(uint result, string operation) {
        if (result != 0) throw new InvalidOperationException(operation + " failed, WinMM code " + result);
    }
    static Format Pcm(ushort channels) {
        return new Format { tag = 1, channels = channels, rate = 48000,
            bytesPerSecond = (uint)(48000 * channels * 2), blockAlign = (ushort)(channels * 2), bits = 16 };
    }
    static Header Read(IntPtr header) { return (Header)Marshal.PtrToStructure(header, typeof(Header)); }
    public static Result Run() { return Run(1, 2000, null, null); }
    public static Result Run(int seconds, int amplitude, string capturePath, string playbackPath) {
        if (seconds < 1 || seconds > 120 || amplitude < 0 || amplitude > 4000)
            throw new ArgumentOutOfRangeException("Test length or amplitude is out of range.");
        uint outId = uint.MaxValue, inId = uint.MaxValue;
        var result = new Result { Seconds = seconds };
        for (uint i = 0; i < waveOutGetNumDevs(); ++i) {
            OutCaps caps; Check(waveOutGetDevCapsW(new UIntPtr(i), out caps, (uint)Marshal.SizeOf(typeof(OutCaps))), "Output caps");
            if (caps.name.StartsWith("Coral-Wave", StringComparison.Ordinal)) { outId = i; result.PlaybackDevice = caps.name; break; }
        }
        for (uint i = 0; i < waveInGetNumDevs(); ++i) {
            InCaps caps; Check(waveInGetDevCapsW(new UIntPtr(i), out caps, (uint)Marshal.SizeOf(typeof(InCaps))), "Input caps");
            if (caps.name.StartsWith("Coral-Wave", StringComparison.Ordinal)) { inId = i; result.CaptureDevice = caps.name; break; }
        }
        if (outId == uint.MaxValue || inId == uint.MaxValue) throw new InvalidOperationException("Coral-Wave playback/capture device not present.");
        IntPtr output = IntPtr.Zero, input = IntPtr.Zero;
        IntPtr outData = IntPtr.Zero, inData = IntPtr.Zero, outHeader = IntPtr.Zero, inHeader = IntPtr.Zero;
        bool outPrepared = false, inPrepared = false;
        uint headerSize = (uint)Marshal.SizeOf(typeof(Header));
        try {
            var outFormat = Pcm(2); var inFormat = Pcm(1);
            Check(waveOutOpen(out output, outId, ref outFormat, IntPtr.Zero, IntPtr.Zero, 8), "Playback open");
            Check(waveInOpen(out input, inId, ref inFormat, IntPtr.Zero, IntPtr.Zero, 8), "Capture open");
            byte[] pcm = new byte[192000 * seconds];
            for (int frame = 0; frame < 48000 * seconds; ++frame) {
                // A short fade at both ends avoids a click in headphones.
                double fade = Math.Min(1.0, Math.Min(frame, 48000 * seconds - 1 - frame) / 480.0);
                short sample = (short)(amplitude * fade * Math.Sin(2 * Math.PI * 440 * frame / 48000));
                for (int ch = 0; ch < 2; ++ch) {
                    pcm[frame * 4 + ch * 2] = (byte)sample;
                    pcm[frame * 4 + ch * 2 + 1] = (byte)(sample >> 8);
                }
            }
            if (!String.IsNullOrEmpty(playbackPath)) {
                byte[] wave = File.ReadAllBytes(playbackPath);
                if (wave.Length != 44 + 96000 * seconds || BitConverter.ToUInt16(wave, 20) != 1 ||
                    BitConverter.ToUInt16(wave, 22) != 1 || BitConverter.ToUInt32(wave, 24) != 48000 ||
                    BitConverter.ToUInt16(wave, 34) != 16)
                    throw new InvalidOperationException("Playback requires a mono 48 kHz/16-bit WAV written by this test.");
                for (int frame = 0; frame < 48000 * seconds; ++frame) {
                    pcm[frame * 4] = pcm[frame * 4 + 2] = wave[44 + frame * 2];
                    pcm[frame * 4 + 1] = pcm[frame * 4 + 3] = wave[45 + frame * 2];
                }
            }
            byte[] captured = new byte[96000 * seconds];
            for (int i = 0; i < captured.Length; ++i) captured[i] = 0x55;
            outData = Marshal.AllocHGlobal(pcm.Length); inData = Marshal.AllocHGlobal(captured.Length);
            Marshal.Copy(pcm, 0, outData, pcm.Length); Marshal.Copy(captured, 0, inData, captured.Length);
            outHeader = Marshal.AllocHGlobal((int)headerSize); inHeader = Marshal.AllocHGlobal((int)headerSize);
            Marshal.StructureToPtr(new Header { data = outData, length = (uint)pcm.Length }, outHeader, false);
            Marshal.StructureToPtr(new Header { data = inData, length = (uint)captured.Length }, inHeader, false);
            Check(waveOutPrepareHeader(output, outHeader, headerSize), "Playback prepare"); outPrepared = true;
            Check(waveInPrepareHeader(input, inHeader, headerSize), "Capture prepare"); inPrepared = true;
            Check(waveInAddBuffer(input, inHeader, headerSize), "Capture queue");
            Check(waveInStart(input), "Capture start");
            Check(waveOutWrite(output, outHeader, headerSize), "Playback write");
            var timer = Stopwatch.StartNew();
            while ((Read(outHeader).flags & 1) == 0 || (Read(inHeader).flags & 1) == 0) {
                if (timer.ElapsedMilliseconds > (seconds + 5) * 1000) throw new TimeoutException("USB audio buffers did not finish. Output flags=" + Read(outHeader).flags + ", input flags=" + Read(inHeader).flags + ", input bytes=" + Read(inHeader).recorded);
                Thread.Sleep(10);
            }
            result.PlaybackBytes = (uint)pcm.Length; result.CaptureBytes = Read(inHeader).recorded;
            if (result.CaptureBytes != captured.Length) throw new InvalidOperationException("Short capture buffer.");
            Marshal.Copy(inData, captured, 0, captured.Length);
            result.CaptureIsSilence = Array.TrueForAll(captured, b => b == 0);
            result.PeaksPerSecond = new int[seconds];
            result.RmsPerSecond = new double[seconds];
            double totalSquares = 0;
            for (int i = 0; i < captured.Length; i += 2) {
                short sample = (short)(captured[i] | (captured[i + 1] << 8));
                if (sample != 0) ++result.CaptureNonzeroSamples;
                result.CapturePeak = Math.Max(result.CapturePeak, Math.Abs((int)sample));
                int second = i / 96000;
                result.PeaksPerSecond[second] = Math.Max(result.PeaksPerSecond[second], Math.Abs((int)sample));
                result.RmsPerSecond[second] += (double)sample * sample;
                totalSquares += (double)sample * sample;
            }
            result.CaptureRms = Math.Sqrt(totalSquares / (captured.Length / 2));
            for (int i = 0; i < seconds; ++i) result.RmsPerSecond[i] = Math.Sqrt(result.RmsPerSecond[i] / 48000);
            if (!String.IsNullOrEmpty(capturePath)) {
                using (var writer = new BinaryWriter(File.Create(capturePath))) {
                    writer.Write(System.Text.Encoding.ASCII.GetBytes("RIFF")); writer.Write(36 + captured.Length);
                    writer.Write(System.Text.Encoding.ASCII.GetBytes("WAVEfmt ")); writer.Write(16);
                    writer.Write((ushort)1); writer.Write((ushort)1); writer.Write(48000); writer.Write(96000);
                    writer.Write((ushort)2); writer.Write((ushort)16);
                    writer.Write(System.Text.Encoding.ASCII.GetBytes("data")); writer.Write(captured.Length); writer.Write(captured);
                }
                result.CaptureFile = Path.GetFullPath(capturePath);
            }
            return result;
        } finally {
            if (output != IntPtr.Zero) waveOutReset(output);
            if (input != IntPtr.Zero) waveInReset(input);
            if (outPrepared) waveOutUnprepareHeader(output, outHeader, headerSize);
            if (inPrepared) waveInUnprepareHeader(input, inHeader, headerSize);
            if (output != IntPtr.Zero) waveOutClose(output);
            if (input != IntPtr.Zero) waveInClose(input);
            foreach (var pointer in new[] { outHeader, inHeader, outData, inData })
                if (pointer != IntPtr.Zero) Marshal.FreeHGlobal(pointer);
        }
    }
}
