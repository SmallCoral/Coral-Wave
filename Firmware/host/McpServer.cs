using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using System.Web.Script.Serialization;

namespace CoralWave {
  // Local stdio JSON-RPC. No TCP port, account, or third-party runtime is needed.
  public static class McpServer {
    static readonly JavaScriptSerializer Json=new JavaScriptSerializer{MaxJsonLength=131072};
    static readonly string[] Versions={"2025-11-25","2025-06-18","2025-03-26","2024-11-05"};
    static readonly int[] HpfHz={122,153,196,245,306,392,490,612};
    static bool initialized;
    static Dictionary<string,object> Obj(object o){var d=o as Dictionary<string,object>;if(d==null)throw new ArgumentException("Expected a JSON object");return d;}
    static Dictionary<string,object> Empty(){return new Dictionary<string,object>();}
    static string Str(Dictionary<string,object> d,string key,string fallback=null){object o;if(!d.TryGetValue(key,out o))return fallback;if(!(o is string))throw new ArgumentException(key+" must be a string");return (string)o;}
    static object Schema(string type){return new {type=type};}
    static object Number(decimal min,decimal max,decimal step){return new {type="number",minimum=min,maximum=max,multipleOf=step};}
    static object ArrayOf(object item){return new {type="array",items=item,minItems=5,maxItems=5};}
    static object EnumOf(params object[] values){return new Dictionary<string,object>{{"enum",values}};}
    static object Input(Dictionary<string,object> props,params string[] required){return new {type="object",properties=props,required=required,additionalProperties=false};}
    static Dictionary<string,object> WithSerial(params KeyValuePair<string,object>[] props){var p=new Dictionary<string,object>{{"serial",new{type="string",description="Board USB serial. Required when more than one board is connected."}}};foreach(var x in props)p.Add(x.Key,x.Value);return p;}
    static object PatchSchema(){return Input(new Dictionary<string,object>{
      {"headphone_db",Number(-57,6,1)},{"dac_preamp_db",Number(-24,0,.5m)},{"microphone_pga_db",Number(-12,35.25m,.75m)},
      {"microphone_boost",Schema("boolean")},{"high_pass",EnumOf("off","audio","speech")},{"high_pass_hz",EnumOf(122,153,196,245,306,392,490,612)},
      {"oversampling_128",Schema("boolean")},{"eq_gain_db",ArrayOf(Number(-12,12,1))},{"eq_frequency_index",ArrayOf(Number(0,3,1))},
      {"eq_wide_mask",Number(0,7,1)},{"stereo_depth_code",Number(0,15,1)},{"limiter",Schema("boolean")},{"limiter_threshold_dbfs",Number(-6,-1,1)},
      {"alc",Schema("boolean")},{"alc_target_dbfs",Number(-22.5m,-1.5m,1.5m)},{"noise_gate",Schema("boolean")},{"noise_gate_dbfs",EnumOf(-39,-45,-51,-57,-63,-69,-75,-81)}
    });}
    static object Tool(string name,string description,object input,bool read,bool destructive=false,bool idempotent=false){return new{name=name,description=description,inputSchema=input,annotations=new{readOnlyHint=read,destructiveHint=destructive,idempotentHint=idempotent,openWorldHint=false}};}
    static object[] Tools(){return new object[]{
      Tool("coral_list_devices","List connected Coral-Wave boards and their serial numbers; application or USB recovery mode.",Input(Empty()),true),
      Tool("coral_get_status","Read MCU, WM8978 ACK, streaming state, error counters and firmware version over USB. WM8978 has no chip ID/readable control registers.",Input(WithSerial()),true),
      Tool("coral_get_audio","Read the firmware settings mirror, with physical units and exact WM8978 ranges. EQ and 3D are fixed to DAC playback.",Input(WithSerial()),true),
      Tool("coral_set_audio","Apply only supplied audio fields. Read-modify-write preserves other controls. Hardware limits are validated. Positive EQ or 3D may need negative dac_preamp_db to avoid clipping. ALC noise gate holds PGA gain; it is not noise cancellation. Windows system volume is independent.",Input(WithSerial(new KeyValuePair<string,object>("patch",PatchSchema())),"patch"),false,false,true),
      Tool("coral_save_to_board","Persist the currently applied settings in a two-row CRC journal. Stop all playback and recording first. The board reboots briefly; firmware updates preserve these settings.",Input(WithSerial()),false,false,false),
      Tool("coral_validate_firmware","Validate a local .cwfw file without touching hardware. Checks board, linked address, vectors, package length and two CRCs. CRC detects corruption, not authenticity.",Input(new Dictionary<string,object>{{"path",Schema("string")}},"path"),true),
      Tool("coral_flash_firmware","Flash a validated local .cwfw file through USB only. Reboots/disconnects audio briefly, writes the application and verifies flash CRC. Bootloader/fuses/settings cannot be overwritten. Interrupted writes remain recoverable. Only invoke for a user-requested firmware update.",Input(WithSerial(new KeyValuePair<string,object>("path",Schema("string"))),"path"),false,true,false),
      Tool("coral_recover_firmware","Recover a non-responsive application over USB. Waits up to 45 seconds to catch the bootloader after the user unplugs/replugs USB, then flashes a validated local .cwfw. Also works after an interrupted update. Tell the user to replug USB after starting this tool.",Input(WithSerial(new KeyValuePair<string,object>("path",Schema("string"))),"path"),false,true,false),
      Tool("coral_enter_bootloader","Reboot this board into USB recovery mode. Audio disconnects until running or flashing application.",Input(WithSerial()),false,false,false),
      Tool("coral_run_firmware","Leave USB recovery mode and boot the installed application only if its vectors and CRC are valid.",Input(WithSerial()),false,false,false)
    };}
    public static int Run(){
      Console.InputEncoding=new UTF8Encoding(false);Console.OutputEncoding=new UTF8Encoding(false);
      string line;
      while((line=Console.ReadLine())!=null){
        if(String.IsNullOrWhiteSpace(line))continue;
        object id=null;bool hasId=false;
        try {
          if(line.Length>131072)throw new ArgumentException("Message too large");
          var q=Obj(Json.DeserializeObject(line));hasId=q.TryGetValue("id",out id);
          if(Str(q,"jsonrpc")!="2.0")throw new ArgumentException("Expected JSON-RPC 2.0");
          string method=Str(q,"method");object paramObject;var p=q.TryGetValue("params",out paramObject)?Obj(paramObject):Empty();
          if(!hasId){if(method=="notifications/initialized")initialized=true;continue;}
          object result;
          if(method=="initialize"){
            string version=Str(p,"protocolVersion");if(!Versions.Contains(version))version=Versions[0];
            result=new{protocolVersion=version,capabilities=new{tools=new{listChanged=false}},serverInfo=new{name="coral-wave",version="3.0.0"},instructions="Local USB control for Coral-Wave SAMD21E18A + WM8978. Start with coral_list_devices and coral_get_status. First bootloader provisioning uses J-Link once; routine updates use USB. All data and operations remain local."};
          }else if(method=="ping")result=new{};
          else if(!initialized){SendError(id,-32002,"Send initialize and notifications/initialized first");continue;}
          else if(method=="tools/list")result=new{tools=Tools()};
          else if(method=="tools/call"){
            string name=Str(p,"name");object a;var arguments=p.TryGetValue("arguments",out a)?Obj(a):Empty();
            try {var value=Call(name,arguments,p);result=new{content=new[]{new{type="text",text=Json.Serialize(value)}},structuredContent=value,isError=false};}
            catch(Exception ex){result=new{content=new[]{new{type="text",text=ex.Message}},isError=true};}
          }else{SendError(id,-32601,"Method not found");continue;}
          Write(new{jsonrpc="2.0",id=id,result=result});
        }catch(Exception ex){SendError(hasId?id:null,ex is ArgumentException?-32602:-32700,ex.Message);}
      }
      return 0;
    }
    static void SendError(object id,int code,string message){Write(new{jsonrpc="2.0",id=id,error=new{code=code,message=message}});}
    static void Write(object o){Console.WriteLine(Json.Serialize(o));Console.Out.Flush();}
    static DeviceEntry Select(Dictionary<string,object> args){
      string serial=Str(args,"serial");var found=Device.Find().Where(x=>String.IsNullOrEmpty(serial)||x.Serial==serial).ToArray();
      if(found.Length!=1)throw new IOException(found.Length==0?"No matching Coral-Wave USB HID device":"Multiple boards connected; specify serial");return found[0];
    }
    static object Status(Device d){
      var i=d.GetInfo();i.Validate(d.Entry.Bootloader);
      object s=null;
      if(!d.Entry.Bootloader){uint[] v=d.Status();s=new{uptime_ms=v[0],stage=v[1],error=v[2],wm8978_i2c_ack=v[3]==1,lrclk_estimate_hz=v[4],playback_active=v[5]!=0,capture_active=v[6]!=0,
        playback_underflows=v[7],dma_errors=v[8],i2s_tx_underruns=v[9],i2s_rx_overruns=v[10],dac_peak_before_codec=v[11],settings_known=v[12]!=0};}
      return new{serial=d.Entry.Serial,mode=d.Entry.Bootloader?"bootloader":"audio",firmware=i.VersionText,mcu_did="0x"+i.Did.ToString("X8"),sample_rate_hz=48000,pcm_bits=16,playback_channels=2,capture_channels=1,status=s};
    }
    static object Audio(Parameters p){return new{
      headphone_db=p.Headphone-57,dac_preamp_db=-p.DacAttenuation*.5,microphone_pga_db=p.MicPga*.75-12,microphone_boost=p.MicBoost==1,
      high_pass=new[]{"off","audio","speech"}[p.Hpf],high_pass_hz=p.Hpf==0?0:p.Hpf==1?3.7:HpfHz[p.HpfCut],oversampling_128=p.Oversample==1,
      eq_gain_db=p.EqGain,eq_frequency_index=p.EqFrequency,eq_wide_mask=p.EqWide,eq_path="dac",stereo_depth_code=p.Depth3d,
      limiter=p.Limiter==1,limiter_threshold_dbfs=-1-p.LimiterLevel,alc=p.Alc==1,alc_target_dbfs=p.AlcTarget*1.5-22.5,noise_gate=p.NoiseGate==1,noise_gate_dbfs=-39-p.GateThreshold*6,
      source="firmware shadow, I2C writes ACK checked; not hardware register readback",eq_frequency_options_hz=new[]{new[]{80,105,135,175},new[]{230,300,385,500},new[]{650,850,1100,1400},new[]{1800,2400,3200,4100},new[]{5300,6900,9000,11700}}
    };}
    static decimal Dec(object o){if(o is bool||o is string||o==null)throw new ArgumentException("Expected a number");return Convert.ToDecimal(o,System.Globalization.CultureInfo.InvariantCulture);}
    static int Code(object value,decimal min,decimal max,decimal step){decimal v=Dec(value);if(v<min||v>max||(v-min)%step!=0)throw new ArgumentException("Value exceeds hardware range or step");return (int)((v-min)/step);}
    static int Bool(object o){if(!(o is bool))throw new ArgumentException("Expected a boolean");return (bool)o?1:0;}
    static int[] Five(object value,int min,int max){var a=value as object[];if(a==null||a.Length!=5)throw new ArgumentException("Expected exactly five values");return a.Select(x=>Code(x,min,max,1)+min).ToArray();}
    static Parameters Patch(Parameters p,Dictionary<string,object> patch){
      foreach(var kv in patch){object v=kv.Value;switch(kv.Key){
        case "headphone_db":p.Headphone=Code(v,-57,6,1);break;
        case "dac_preamp_db":p.DacAttenuation=-Code(v,-24,0,.5m)+48;break;
        case "microphone_pga_db":p.MicPga=Code(v,-12,35.25m,.75m);break;
        case "microphone_boost":p.MicBoost=Bool(v);break;
        case "high_pass":p.Hpf=Array.IndexOf(new[]{"off","audio","speech"},v as string);if(p.Hpf<0)throw new ArgumentException("Invalid high_pass mode");break;
        case "high_pass_hz":p.HpfCut=Array.IndexOf(HpfHz,(int)Dec(v));if(p.HpfCut<0||Dec(v)!=(int)Dec(v))throw new ArgumentException("Invalid high-pass cutoff for 48 kHz");break;
        case "oversampling_128":p.Oversample=Bool(v);break;
        case "eq_gain_db":p.EqGain=Five(v,-12,12);break;
        case "eq_frequency_index":p.EqFrequency=Five(v,0,3);break;
        case "eq_wide_mask":p.EqWide=Code(v,0,7,1);break;
        case "stereo_depth_code":p.Depth3d=Code(v,0,15,1);break;
        case "limiter":p.Limiter=Bool(v);break;
        case "limiter_threshold_dbfs":p.LimiterLevel=5-Code(v,-6,-1,1);break;
        case "alc":p.Alc=Bool(v);break;
        case "alc_target_dbfs":p.AlcTarget=Code(v,-22.5m,-1.5m,1.5m);break;
        case "noise_gate":p.NoiseGate=Bool(v);break;
        case "noise_gate_dbfs":p.GateThreshold=7-Code(v,-81,-39,6);break;
        default:throw new ArgumentException("Unsupported audio field "+kv.Key);
      }}p.Validate();return p;
    }
    static object Call(string name,Dictionary<string,object> a,Dictionary<string,object> request){
      string[] allowed=name=="coral_set_audio"?new[]{"serial","patch"}:name=="coral_flash_firmware"||name=="coral_recover_firmware"?new[]{"serial","path"}:name=="coral_validate_firmware"?new[]{"path"}:name=="coral_list_devices"?new string[0]:new[]{"serial"};
      foreach(string key in a.Keys)if(!allowed.Contains(key))throw new ArgumentException("Unknown argument "+key);
      if(name=="coral_list_devices")return new{devices=Device.Find().Select(x=>new{serial=x.Serial,mode=x.Bootloader?"bootloader":"audio"}).ToArray()};
      if(name=="coral_validate_firmware") {var f=FirmwareImage.Load(RequiredPath(a));return new{valid=true,board="Coral-Wave / ATSAMD21E18A",version=f.VersionText,bytes=f.Data.Length,crc32=f.Crc.ToString("X8")};}
      if(name=="coral_flash_firmware"||name=="coral_recover_firmware") {
        var f=FirmwareImage.Load(RequiredPath(a));var e=name=="coral_recover_firmware"?Device.WaitRecovery(Str(a,"serial"),45000):Select(a);object meta;object token=null;
        if(request.TryGetValue("_meta",out meta)){object t;if(Obj(meta).TryGetValue("progressToken",out t))token=t;}
        int last=-1;var result=f.Upload(e,(percent,text)=>{if(token!=null&&percent>last){last=percent;Write(new{jsonrpc="2.0",method="notifications/progress",@params=new{progressToken=token,progress=percent,total=100,message=text}});}});
        return new{flashed=true,serial=result.Serial,version=f.VersionText,flash_crc32=f.Crc.ToString("X8")};
      }
      string[] known={"coral_get_status","coral_get_audio","coral_set_audio","coral_save_to_board","coral_enter_bootloader","coral_run_firmware"};
      if(!known.Contains(name))throw new ArgumentException("Unknown tool "+name);
      var entry=Select(a);
      using(var d=new Device(entry)) {
        d.GetInfo().Validate(entry.Bootloader);
        if(name=="coral_get_status")return Status(d);
        if(name=="coral_get_audio")return Audio(d.GetParameters());
        if(name=="coral_set_audio"){object patch;if(!a.TryGetValue("patch",out patch))throw new ArgumentException("patch is required");return Audio(d.Apply(Patch(d.GetParameters(),Obj(patch))));}
        if(name=="coral_save_to_board"){var p=d.GetParameters();d.Command(5);Thread.Sleep(1200);var e=Device.WaitFor(entry.Serial,false,15000,false);using(var next=new Device(e)){if(!p.Encode().SequenceEqual(next.GetParameters().Encode()))throw new IOException("Saved settings verification failed");}return new{saved=true,rebooted=true,serial=entry.Serial};}
        if(name=="coral_enter_bootloader"){d.Command(entry.Bootloader?(byte)16:(byte)6);Device.WaitFor(entry.Serial,true,15000,true);return new{mode="bootloader",serial=entry.Serial};}
        d.Command(20);Device.WaitFor(entry.Serial,false,15000,false);return new{mode="audio",serial=entry.Serial};
      }
    }
    static string RequiredPath(Dictionary<string,object> a){string p=Str(a,"path");if(String.IsNullOrEmpty(p)||!Path.IsPathRooted(p))throw new ArgumentException("path must be an absolute local file path");return p;}
  }
}
