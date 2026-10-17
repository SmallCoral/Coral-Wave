using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using Microsoft.Win32.SafeHandles;

namespace CoralWave {
  public sealed class DeviceEntry {
    public string Path, Serial;
    public bool Bootloader;
    public override string ToString() { return (Bootloader ? "恢复模式  ·  " : "Coral-Wave  ·  ") + Serial; }
  }
  public sealed class Device : IDisposable {
    public const uint Board=0x53443231, AppBase=0x4000, AppMax=0x3BC00;
    public readonly DeviceEntry Entry;
    SafeFileHandle handle;
    Mutex lease;
    byte sequence;
    public Device(DeviceEntry entry) {
      Entry=entry;
      lease=new Mutex(false,"Local\\CoralWave.USB."+entry.Serial);
      bool acquired=false;
      try {
        try{acquired=lease.WaitOne(800);}catch(AbandonedMutexException){acquired=true;}
        if(!acquired)throw new IOException("板子正由另一个 Coral-Wave 工具操作，请稍后再试。");
        handle=Native.CreateFile(entry.Path,0xC0000000,3,IntPtr.Zero,3,0,IntPtr.Zero);
        if(handle.IsInvalid) throw new Win32Exception(Marshal.GetLastWin32Error(),"无法打开 Coral-Wave USB 控制接口");
      }catch{if(acquired)lease.ReleaseMutex();lease.Dispose();lease=null;if(handle!=null)handle.Dispose();throw;}
    }
    public void Dispose() { if(handle!=null){handle.Dispose();handle=null;}if(lease!=null){lease.ReleaseMutex();lease.Dispose();lease=null;} }
    public static List<DeviceEntry> Find() {
      var found=new List<DeviceEntry>(); Guid guid;Native.HidD_GetHidGuid(out guid);
      IntPtr list=Native.SetupDiGetClassDevs(ref guid,null,IntPtr.Zero,0x12);
      if(list==new IntPtr(-1))throw new Win32Exception();
      try {
        for(uint i=0;;++i) {
          var itf=new Native.InterfaceData();itf.Size=Marshal.SizeOf(itf);
          if(!Native.SetupDiEnumDeviceInterfaces(list,IntPtr.Zero,ref guid,i,ref itf)) {
            if(Marshal.GetLastWin32Error()!=259)throw new Win32Exception();break;
          }
          uint required;
          Native.SetupDiGetDeviceInterfaceDetail(list,ref itf,IntPtr.Zero,0,out required,IntPtr.Zero);
          IntPtr detail=Marshal.AllocHGlobal((int)required);
          try {
            Marshal.WriteInt32(detail,IntPtr.Size==8?8:6);
            if(!Native.SetupDiGetDeviceInterfaceDetail(list,ref itf,detail,required,out required,IntPtr.Zero))continue;
            string path=Marshal.PtrToStringUni(IntPtr.Add(detail,4));
            string lower=path.ToLowerInvariant();
            if(!lower.Contains("vid_cafe&pid_c0a4")&&!lower.Contains("vid_cafe&pid_c0b0"))continue;
            using(var h=Native.CreateFile(path,0,3,IntPtr.Zero,3,0,IntPtr.Zero)) {
              if(h.IsInvalid)continue;
              IntPtr capsData;
              if(!Native.HidD_GetPreparsedData(h,out capsData))continue;
              Native.Caps caps;
              try { if(Native.HidP_GetCaps(capsData,out caps)!=0x110000)continue; }
              finally {Native.HidD_FreePreparsedData(capsData);}
              if(caps.UsagePage!=0xFF00||caps.Usage!=1||caps.FeatureBytes!=64)continue;
              byte[] serial=new byte[256];
              if(!Native.HidD_GetSerialNumberString(h,serial,serial.Length))continue;
              found.Add(new DeviceEntry{Path=path,Serial=Encoding.Unicode.GetString(serial).TrimEnd('\0'),Bootloader=lower.Contains("pid_c0b0")});
            }
          } finally {Marshal.FreeHGlobal(detail);}
        }
      } finally {Native.SetupDiDestroyDeviceInfoList(list);}
      return found;
    }
    public byte[] Command(byte command, byte[] data=null) {
      if(data==null)data=new byte[0];
      if(data.Length>55)throw new ArgumentOutOfRangeException("data");
      byte[] packet=new byte[64];packet[0]=1;packet[1]=(byte)'C';packet[2]=(byte)'W';packet[3]=1;
      packet[4]=command;packet[5]=++sequence;packet[7]=(byte)data.Length;
      Buffer.BlockCopy(data,0,packet,9,data.Length);
      if(!Native.HidD_SetFeature(handle,packet,packet.Length))throw new Win32Exception(Marshal.GetLastWin32Error(),"USB 命令发送失败");
      var watch=System.Diagnostics.Stopwatch.StartNew();
      do {
        byte[] reply=new byte[64];reply[0]=1;
        if(!Native.HidD_GetFeature(handle,reply,reply.Length))throw new Win32Exception(Marshal.GetLastWin32Error(),"USB 应答读取失败");
        if(reply[1]=='C'&&reply[2]=='W'&&reply[3]==1&&reply[4]==command&&reply[5]==sequence) {
          if(reply[6]==6){Thread.Sleep(2);continue;}
          if(reply[6]!=0)throw new IOException("设备拒绝命令 0x"+command.ToString("X2")+"，"+Error(reply[6]));
          if(reply[7]>55)throw new IOException("USB 应答长度无效");
          byte[] result=new byte[reply[7]];Buffer.BlockCopy(reply,9,result,0,result.Length);return result;
        }
        Thread.Sleep(2);
      } while(watch.ElapsedMilliseconds<6000);
      throw new TimeoutException("Coral-Wave 应答超时。可重新连接 USB 后再试。");
    }
    static string Error(byte code) {
      switch(code){case 1:return "参数或格式无效";case 2:return "当前状态不允许操作，请先停止录放音";
        case 3:return "Flash 写入或校验失败";case 4:return "固件 CRC 或向量表校验失败";
        case 5:return "WM8978 未应答，设置状态待确认";default:return "错误码 "+code;}
    }
    public Info GetInfo() { return new Info(Command(1)); }
    public uint[] Status() {
      byte[] b=Command(2);if(b.Length!=52)throw new IOException("状态协议不匹配");
      uint[] result=new uint[13];for(int i=0;i<13;++i)result[i]=BitConverter.ToUInt32(b,i*4);return result;
    }
    public Parameters GetParameters() { return Parameters.Decode(Command(3)); }
    public Parameters Apply(Parameters p) { p.Validate();return Parameters.Decode(Command(4,p.Encode())); }
    public ushort[] Registers(byte start) {
      byte[] b=Command(7,new byte[]{start});if(b.Length!=20)throw new IOException("寄存器镜像长度无效");
      ushort[] r=new ushort[10];for(int i=0;i<10;++i)r[i]=BitConverter.ToUInt16(b,i*2);return r;
    }
    public static DeviceEntry WaitFor(string serial,bool boot,int milliseconds,bool hold) {
      var w=System.Diagnostics.Stopwatch.StartNew();
      do {
        foreach(var e in Find())if(e.Serial==serial&&e.Bootloader==boot) {
          try {using(var d=new Device(e)){d.GetInfo().Validate(boot);if(hold)d.Command(16);return e;}}
          catch(Win32Exception){}catch(IOException){}
        }
        Thread.Sleep(150);
      }while(w.ElapsedMilliseconds<milliseconds);
      throw new TimeoutException(boot?"没有找到 USB 恢复接口。保持此窗口打开，重新插入板子的 USB 后重试。":"固件已写入，但声卡尚未重新连接。请重新插入 USB。");
    }
    public static DeviceEntry WaitRecovery(string serial,int milliseconds) {
      var w=System.Diagnostics.Stopwatch.StartNew();
      do {
        var entries=Find().FindAll(x=>x.Bootloader&&(String.IsNullOrEmpty(serial)||x.Serial==serial));
        if(entries.Count>1)throw new IOException("检测到多个恢复接口，请指定板子的序列号。");
        if(entries.Count==1)try{using(var d=new Device(entries[0])){d.GetInfo().Validate(true);d.Command(16);return entries[0];}}catch(Win32Exception){}
        Thread.Sleep(100);
      }while(w.ElapsedMilliseconds<milliseconds);
      throw new TimeoutException("等待恢复接口超时。点击恢复烧录后，再拔插板子的 USB-C。");
    }
  }
  public sealed class Info {
    public uint BoardId,Version,Base,Max,Did,Mode;
    public string VersionText {get {return (Version>>16)+"."+((Version>>8)&255)+"."+(Version&255);}}
    public Info(byte[] b) {
      if(b.Length!=44)throw new IOException("设备信息协议不匹配");
      BoardId=BitConverter.ToUInt32(b,0);Version=BitConverter.ToUInt32(b,4);Base=BitConverter.ToUInt32(b,8);
      Max=BitConverter.ToUInt32(b,12);Did=BitConverter.ToUInt32(b,16);Mode=BitConverter.ToUInt32(b,20);
    }
    public void Validate(bool boot) {if(BoardId!=Device.Board||Base!=Device.AppBase||Max!=Device.AppMax||Mode!=(boot?1u:0u))throw new IOException("设备型号或分区协议不匹配");}
  }
  public sealed class FirmwareImage {
    public byte[] Data;public uint Crc,Version;
    public string VersionText {get {return (Version>>16)+"."+((Version>>8)&255)+"."+(Version&255);}}
    public static uint Crc32(byte[] data,int offset,int count) {
      uint crc=0xFFFFFFFF;
      for(int i=offset;i<offset+count;++i){crc^=data[i];for(int b=0;b<8;++b)crc=(crc>>1)^((0u-(crc&1))&0xEDB88320);}
      return ~crc;
    }
    public static FirmwareImage Load(string path) {
      byte[] b=File.ReadAllBytes(path);
      if(b.Length<256||Encoding.ASCII.GetString(b,0,8)!="CWFWPKG1")throw new IOException("请选择 Coral-Wave .cwfw 固件包");
      uint length=BitConverter.ToUInt32(b,16),crc=BitConverter.ToUInt32(b,20);
      if(BitConverter.ToUInt32(b,8)!=Device.Board||BitConverter.ToUInt32(b,12)!=Device.AppBase||length<192||length>Device.AppMax||b.Length!=64+length)
        throw new IOException("固件型号、地址或长度无效");
      if(Crc32(b,0,60)!=BitConverter.ToUInt32(b,60)||Crc32(b,64,(int)length)!=crc)throw new IOException("固件包 CRC 校验失败，请重新选择完整文件");
      uint sp=BitConverter.ToUInt32(b,64),pc=BitConverter.ToUInt32(b,68);
      if(sp<=0x20000000||sp>0x20008000||(sp&7)!=0||(pc&1)==0||(pc&~1u)<Device.AppBase||(pc&~1u)>=Device.AppBase+length)throw new IOException("固件的主控向量表无效");
      byte[] data=new byte[length];Buffer.BlockCopy(b,64,data,0,data.Length);
      return new FirmwareImage{Data=data,Crc=crc,Version=BitConverter.ToUInt32(b,24)};
    }
    public DeviceEntry Upload(DeviceEntry entry,Action<int,string> progress) {
      using(var d=new Device(entry)) {
        d.GetInfo().Validate(entry.Bootloader);
        if(entry.Bootloader)d.Command(16);else d.Command(6);
      }
      progress(0,"等待 USB 恢复接口…");
      DeviceEntry boot=Device.WaitFor(entry.Serial,true,18000,true);
      using(var d=new Device(boot)) {
        byte[] begin=new byte[16];Buffer.BlockCopy(BitConverter.GetBytes(Device.Board),0,begin,0,4);
        Buffer.BlockCopy(BitConverter.GetBytes((uint)Data.Length),0,begin,4,4);
        Buffer.BlockCopy(BitConverter.GetBytes(Crc),0,begin,8,4);Buffer.BlockCopy(BitConverter.GetBytes(Version),0,begin,12,4);
        d.Command(17,begin);
        for(int offset=0;offset<Data.Length;) {
          int n=Math.Min(48,Data.Length-offset);byte[] block=new byte[n+4];
          Buffer.BlockCopy(BitConverter.GetBytes(offset),0,block,0,4);Buffer.BlockCopy(Data,offset,block,4,n);
          byte[] reply=d.Command(18,block);offset+=n;
          if(reply.Length!=4||BitConverter.ToUInt32(reply,0)!=offset)throw new IOException("固件写入偏移校验失败");
          progress(offset*90/Data.Length,"USB 写入 "+offset+" / "+Data.Length+" 字节");
        }
        progress(94,"回读 Flash 并校验 CRC…");byte[] metadata=d.Command(19);
        if(metadata.Length!=28||BitConverter.ToUInt32(metadata,4)!=Data.Length||BitConverter.ToUInt32(metadata,8)!=Crc||BitConverter.ToUInt32(metadata,12)!=Version)
          throw new IOException("设备最终校验结果不匹配");
        d.Command(20);
      }
      progress(98,"等待声卡重新连接…");var app=Device.WaitFor(entry.Serial,false,20000,false);
      using(var d=new Device(app))if(d.GetInfo().Version!=Version)throw new IOException("重启后的固件版本不匹配");
      progress(100,"USB 升级完成，声卡已重新连接");return app;
    }
  }
  public sealed class Parameters {
    public int Schema=1,Headphone=39,MicPga=40,MicBoost=1,Hpf=1,HpfCut=0,Oversample=1,EqPath=1;
    public int[] EqGain=new int[5],EqFrequency=new int[]{1,1,1,1,1};
    public int EqWide=0,Depth3d=0,Limiter=0,LimiterLevel=2,Alc=0,AlcTarget=11,NoiseGate=0,GateThreshold=3,DacAttenuation=0;
    public byte[] Encode() {
      byte[] p=new byte[32];int[] first={Schema,Headphone,MicPga,MicBoost,Hpf,HpfCut,Oversample,EqPath};
      for(int i=0;i<8;++i)p[i]=(byte)first[i];
      for(int i=0;i<5;++i){p[8+i]=unchecked((byte)(sbyte)EqGain[i]);p[13+i]=(byte)EqFrequency[i];}
      int[] tail={EqWide,Depth3d,Limiter,LimiterLevel,Alc,AlcTarget,NoiseGate,GateThreshold};
      for(int i=0;i<8;++i)p[18+i]=(byte)tail[i];p[26]=(byte)DacAttenuation;return p;
    }
    public static Parameters Decode(byte[] p) {
      if(p.Length!=32)throw new IOException("参数协议不匹配");
      var r=new Parameters{Schema=p[0],Headphone=p[1],MicPga=p[2],MicBoost=p[3],Hpf=p[4],HpfCut=p[5],Oversample=p[6],EqPath=p[7],
        EqWide=p[18],Depth3d=p[19],Limiter=p[20],LimiterLevel=p[21],Alc=p[22],AlcTarget=p[23],NoiseGate=p[24],GateThreshold=p[25],DacAttenuation=p[26]};
      for(int i=0;i<5;++i){r.EqGain[i]=(sbyte)p[8+i];r.EqFrequency[i]=p[13+i];}
      for(int i=27;i<32;++i)if(p[i]!=0)throw new IOException("预设包含不支持的扩展参数");r.Validate();return r;
    }
    static void Range(int n,int min,int max){if(n<min||n>max)throw new IOException("音频参数超出 WM8978 支持的范围");}
    public void Validate() {
      if(Schema!=1||EqPath!=1||EqGain==null||EqGain.Length!=5||EqFrequency==null||EqFrequency.Length!=5)throw new IOException("预设格式或 EQ 路径不支持");
      Range(Headphone,0,63);Range(MicPga,0,63);Range(MicBoost,0,1);Range(Hpf,0,2);Range(HpfCut,0,7);Range(Oversample,0,1);
      Range(EqWide,0,7);Range(Depth3d,0,15);Range(Limiter,0,1);Range(LimiterLevel,0,5);Range(Alc,0,1);Range(AlcTarget,0,14);
      Range(NoiseGate,0,1);Range(GateThreshold,0,7);Range(DacAttenuation,0,48);
      for(int i=0;i<5;++i){Range(EqGain[i],-12,12);Range(EqFrequency[i],0,3);}
      if(NoiseGate==1&&Alc==0)throw new IOException("噪声门需要同时启用自动增益 ALC");
    }
  }
  static class Native {
    [StructLayout(LayoutKind.Sequential)]public struct InterfaceData {public int Size;public Guid Guid;public int Flags;public IntPtr Reserved;}
    [StructLayout(LayoutKind.Sequential)]public struct Caps {
      public ushort Usage,UsagePage,InputBytes,OutputBytes,FeatureBytes;
      [MarshalAs(UnmanagedType.ByValArray,SizeConst=17)]public ushort[] Reserved;
      public ushort LinkNodes,InputButtonCaps,InputValueCaps,InputDataIndices,OutputButtonCaps,OutputValueCaps,OutputDataIndices,FeatureButtonCaps,FeatureValueCaps,FeatureDataIndices;
    }
    [DllImport("hid.dll")]public static extern void HidD_GetHidGuid(out Guid guid);
    [DllImport("hid.dll",SetLastError=true)] [return:MarshalAs(UnmanagedType.Bool)]public static extern bool HidD_GetFeature(SafeFileHandle h,byte[] data,int length);
    [DllImport("hid.dll",SetLastError=true)] [return:MarshalAs(UnmanagedType.Bool)]public static extern bool HidD_SetFeature(SafeFileHandle h,byte[] data,int length);
    [DllImport("hid.dll",SetLastError=true)] [return:MarshalAs(UnmanagedType.Bool)]public static extern bool HidD_GetSerialNumberString(SafeFileHandle h,byte[] data,int length);
    [DllImport("hid.dll")] [return:MarshalAs(UnmanagedType.Bool)]public static extern bool HidD_GetPreparsedData(SafeFileHandle h,out IntPtr p);
    [DllImport("hid.dll")] [return:MarshalAs(UnmanagedType.Bool)]public static extern bool HidD_FreePreparsedData(IntPtr p);
    [DllImport("hid.dll")]public static extern int HidP_GetCaps(IntPtr p,out Caps caps);
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)]public static extern SafeFileHandle CreateFile(string path,uint access,uint share,IntPtr security,uint mode,uint flags,IntPtr template);
    [DllImport("setupapi.dll",CharSet=CharSet.Unicode,SetLastError=true)]public static extern IntPtr SetupDiGetClassDevs(ref Guid guid,string enumerator,IntPtr parent,uint flags);
    [DllImport("setupapi.dll",SetLastError=true)] [return:MarshalAs(UnmanagedType.Bool)]public static extern bool SetupDiEnumDeviceInterfaces(IntPtr list,IntPtr device,ref Guid guid,uint index,ref InterfaceData data);
    [DllImport("setupapi.dll",CharSet=CharSet.Unicode,SetLastError=true)] [return:MarshalAs(UnmanagedType.Bool)]public static extern bool SetupDiGetDeviceInterfaceDetail(IntPtr list,ref InterfaceData data,IntPtr detail,uint size,out uint required,IntPtr device);
    [DllImport("setupapi.dll")] [return:MarshalAs(UnmanagedType.Bool)]public static extern bool SetupDiDestroyDeviceInfoList(IntPtr list);
  }
}
