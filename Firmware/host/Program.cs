using System;
using System.IO;
using System.Linq;
using System.Threading;
using System.Web.Script.Serialization;
using System.Windows.Forms;

namespace CoralWave {
  static class Program {
    static JavaScriptSerializer json=new JavaScriptSerializer();
    [STAThread] static int Main(string[] args) {
      try {
        if(args.Length>0)Console.OutputEncoding=new System.Text.UTF8Encoding(false);
        if(args.Length==1&&args[0]=="--mcp")return McpServer.Run();
        if(args.Length==1&&args[0]=="--ui-test"){Application.EnableVisualStyles();Print(new MainForm().TestLayout());return 0;}
        if(args.Length==0){Application.EnableVisualStyles();Application.SetCompatibleTextRenderingDefault(false);Application.Run(new MainForm());return 0;}
        if(args[0]=="--preview"&&args.Length>=2){Application.EnableVisualStyles();new MainForm().RenderPreview(args[1],args.Length>2?Int32.Parse(args[2]):0);return 0;}
        if(args[0]=="--validate"&&args.Length==2){var image=FirmwareImage.Load(args[1]);Print(new{valid=true,version=image.VersionText,bytes=image.Data.Length,crc32=image.Crc.ToString("X8")});return 0;}
        if(args[0]=="--recover"&&args.Length==2){var f=FirmwareImage.Load(args[1]);var boot=Device.WaitRecovery(Environment.GetEnvironmentVariable("CORAL_WAVE_SERIAL"),45000);f.Upload(boot,(n,s)=>{if(n==0||n>=94)Console.WriteLine(n+"% "+s);});return 0;}
        var all=Device.Find();
        if(args[0]=="--list"){Print(all);return 0;}
        string serial=Environment.GetEnvironmentVariable("CORAL_WAVE_SERIAL");
        var candidates=all.Where(x=>String.IsNullOrEmpty(serial)||x.Serial==serial).ToArray();
        if(candidates.Length!=1)throw new IOException(candidates.Length==0?"没有找到 Coral-Wave USB HID 接口":"检测到多块板子，请用 CORAL_WAVE_SERIAL 指定序列号");
        var e=candidates[0];
        if(args[0]=="--flash"&&args.Length==2){var result=FirmwareImage.Load(args[1]).Upload(e,(n,s)=>{if(n==0||n>=94)Console.WriteLine(n+"% "+s);});Print(new{flashed=true,serial=result.Serial});return 0;}
        using(var d=new Device(e)) {
          d.GetInfo().Validate(e.Bootloader);
          switch(args[0]) {
          case "--status":Print(new{device=e,info=d.GetInfo(),status=e.Bootloader?null:d.Status(),parameters=e.Bootloader?null:d.GetParameters()});break;
          case "--export":Require(args,2);File.WriteAllText(args[1],json.Serialize(d.GetParameters()));break;
          case "--apply":Require(args,2);Print(d.Apply(json.Deserialize<Parameters>(File.ReadAllText(args[1]))));break;
          case "--boot":d.Command(e.Bootloader?(byte)16:(byte)6);Device.WaitFor(e.Serial,true,15000,true);break;
          case "--run":d.Command(20);Device.WaitFor(e.Serial,false,15000,false);break;
          case "--save":d.Command(5);Thread.Sleep(1200);Device.WaitFor(e.Serial,false,15000,false);break;
          case "--self-test":SelfTest(d);break;
          case "--interrupt-test":Interrupt(d);break;
          default:throw new IOException("支持 --list --status --validate FILE --flash FILE --export FILE --apply FILE --save --boot --run --self-test --interrupt-test");
          }
        }
        return 0;
      }catch(Exception ex){Console.Error.WriteLine(ex.Message);return 1;}
    }
    static void Print(object o){Console.WriteLine(json.Serialize(o));}
    static void Require(string[] a,int count){if(a.Length!=count)throw new IOException("命令参数不完整");}
    static void Check(bool condition,string text){if(!condition)throw new IOException("测试失败  "+text);}
    static void SelfTest(Device d) {
      var old=d.GetParameters();uint[] before=d.Status();int cycles=0;
      try {
        for(int n=0;n<24;++n){
          var p=Parameters.Decode(old.Encode());
          p.Headphone=Math.Min(39,old.Headphone);p.EqGain=new[]{n%7-3,2,-2,1,0};p.EqFrequency=new[]{n%4,1,2,3,1};p.EqWide=n%8;
          p.DacAttenuation=12;p.Depth3d=n%4;p.Limiter=n%2;p.LimiterLevel=n%6;
          p.MicPga=32+n%8;p.MicBoost=n%2;p.Hpf=n%3;p.HpfCut=n%8;p.Oversample=n%2;
          p.Alc=n%2;p.NoiseGate=p.Alc;p.AlcTarget=7+n%7;p.GateThreshold=n%8;
          Check(d.Apply(p).Encode().SequenceEqual(p.Encode()),"参数回显");
          Check(d.GetParameters().Encode().SequenceEqual(p.Encode()),"参数镜像");
          var eq=d.Registers(18);
          for(int i=0;i<5;++i){int expected=12-p.EqGain[i]|(p.EqFrequency[i]<<5);if(i==0||i>0&&i<4&&(p.EqWide&(1<<(i-1)))!=0)expected|=256;Check(eq[i]==expected,"EQ 寄存器 "+i);}
          var gain=d.Registers(44);Check(gain[1]==(256|p.MicPga)&&gain[3]==(p.MicBoost==1?256:0),"麦克风增益");
          Check(gain[8]==p.Headphone&&gain[9]==(256|p.Headphone),"耳机增益");
          var alc=d.Registers(28);Check(alc[4]==((p.Alc==1?256:0)|40),"ALC 左通道选择");
          Check(alc[5]==(32|p.AlcTarget)&&alc[7]==((p.NoiseGate==1?8:0)|p.GateThreshold),"ALC 目标和噪声门");
          ++cycles;
        }
        var invalid=old.Encode();invalid[8]=13;bool rejected=false;
        try{d.Command(4,invalid);}catch(IOException){rejected=true;}Check(rejected,"越界 EQ 拒绝");
        Check(d.Status()[2]==0&&d.Status()[3]==1,"主控状态和 I2C ACK");
      }finally{d.Apply(old);}
      uint[] after=d.Status();
      Print(new{passed=true,cycles=cycles,restored=true,dmaErrors=after[8]-before[8],txUnderruns=after[9]-before[9],rxOverruns=after[10]-before[10],playbackUnderflows=after[7]-before[7]});
    }
    static void Interrupt(Device d) {
      Check(d.Entry.Bootloader,"先进入恢复模式");d.Command(16);
      byte[] start=new byte[16];Buffer.BlockCopy(BitConverter.GetBytes(Device.Board),0,start,0,4);
      Buffer.BlockCopy(BitConverter.GetBytes(256u),0,start,4,4);d.Command(17,start);
      d.Command(18,new byte[52]);d.Command(21);
      bool rejected=false;try{d.Command(20);}catch(IOException){rejected=true;}
      Check(rejected,"无效固件不得启动");Print(new{interrupted=true,invalidImageRejected=true,recoveryUsbAvailable=d.GetInfo().Mode==1});
    }
  }
}
