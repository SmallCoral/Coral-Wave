using System;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using System.Windows.Forms;
using System.Web.Script.Serialization;

namespace CoralWave {
  sealed class BufferedFlow : FlowLayoutPanel {
    public BufferedFlow(){DoubleBuffered=true;SetStyle(ControlStyles.OptimizedDoubleBuffer|ControlStyles.AllPaintingInWmPaint,true);}
  }
  sealed class AudioPage : FlowLayoutPanel {
    bool sizing,queued;
    public AudioPage(){DoubleBuffered=true;FlowDirection=FlowDirection.TopDown;WrapContents=false;AutoScroll=true;Padding=new Padding(10);Dock=DockStyle.Fill;}
    protected override void OnSizeChanged(EventArgs e){base.OnSizeChanged(e);QueueWidth();}
    protected override void OnControlAdded(ControlEventArgs e){base.OnControlAdded(e);QueueWidth();}
    protected override void OnHandleCreated(EventArgs e){base.OnHandleCreated(e);QueueWidth();}
    void QueueWidth(){if(!IsHandleCreated||queued||sizing)return;queued=true;BeginInvoke((Action)delegate{queued=false;if(!IsDisposed)FitWidth();});}
    void FitWidth(){
      if(sizing)return;sizing=true;SuspendLayout();
      try {
        // Reserve the scrollbar regardless of visibility. Its appearance must not
        // change content width and trigger another scrollbar/layout transition.
        int w=Math.Max(200,Width-Padding.Horizontal-SystemInformation.VerticalScrollBarWidth-6);
        foreach(Control c in Controls){if(c.Width==w&&c.MinimumSize.Width==w&&c.MaximumSize.Width==w)continue;
          c.MinimumSize=Size.Empty;c.MaximumSize=new Size(w,0);c.MinimumSize=new Size(w,0);c.Width=w;}
      }finally{ResumeLayout(true);sizing=false;}
    }
  }
  public sealed class MainForm : Form {
    protected override CreateParams CreateParams { get {var cp=base.CreateParams;cp.ExStyle|=0x02000000;return cp;} }
    static readonly Color Coral=Color.FromArgb(250,127,80),Ink=Color.FromArgb(35,53,66),Muted=Color.FromArgb(92,110,119);
    ComboBox devices=new ComboBox(), preset=new ComboBox();
    Label connection=new Label(), detail=new Label(), message=new Label(), packageLabel=new Label();
    Button apply,read,saveDevice,flash;
    TabControl tabs=new TabControl();
    ProgressBar progress=new ProgressBar();TextBox log=new TextBox();
    NumericUpDown headphone=Number(-57,6,1,0),mic=Number(-12,35.25m,.75m,2),preamp=Number(-24,0,.5m,1);
    ComboBox hpf=Choice("关闭","音频模式 · 3.7 Hz","语音模式 · 可选截止频率"),hpfCut=Choice("122 Hz","153 Hz","196 Hz","245 Hz","306 Hz","392 Hz","490 Hz","612 Hz");
    CheckBox boost=Check("麦克风模拟提升 +20 dB"),osr=Check("128 倍 ADC / DAC 过采样"),limiter=Check("启用 DAC 限幅器"),alc=Check("启用麦克风自动增益 ALC"),gate=Check("启用 ALC 噪声门"),live=Check("改动后自动应用");
    ComboBox limitLevel=Choice("-1 dBFS","-2 dBFS","-3 dBFS","-4 dBFS","-5 dBFS","-6 dBFS");
    ComboBox gateLevel=Choice("-39 dBFS","-45 dBFS","-51 dBFS","-57 dBFS","-63 dBFS","-69 dBFS","-75 dBFS","-81 dBFS");
    NumericUpDown alcTarget=Number(-22.5m,-1.5m,1.5m,1),depth=Number(0,100,6.6666667m,0);
    NumericUpDown[] gains=new NumericUpDown[5];ComboBox[] frequencies=new ComboBox[5];CheckBox[] widths=new CheckBox[3];
    DeviceEntry selected;FirmwareImage image;string selectedSerial,imagePath;
    bool busy,scanning,loading,dirty,connected,closing;
    long editRevision;
    System.Windows.Forms.Timer timer=new System.Windows.Forms.Timer(),debounce=new System.Windows.Forms.Timer();
    static NumericUpDown Number(decimal min,decimal max,decimal step,int digits) {return new NumericUpDown{Minimum=min,Maximum=max,Increment=step,DecimalPlaces=digits,Width=128,Anchor=AnchorStyles.Left,ThousandsSeparator=false};}
    static ComboBox Choice(params string[] items) {var c=new ComboBox{DropDownStyle=ComboBoxStyle.DropDownList,Width=260,Anchor=AnchorStyles.Left};c.Items.AddRange(items);if(items.Length>0)c.SelectedIndex=0;return c;}
    static CheckBox Check(string text) {return new CheckBox{Text=text,AutoSize=true,Anchor=AnchorStyles.Left,Padding=new Padding(0,5,0,5)};}
    static Label TextLabel(string text) {return new Label{Text=text,AutoSize=true,ForeColor=Muted,Anchor=AnchorStyles.Left,Margin=new Padding(3,8,16,8)};}
    static Button ButtonOf(string text,EventHandler click,bool primary=false) {
      var b=new Button{Text=text,AutoSize=true,MinimumSize=new Size(0,38),Padding=new Padding(12,5,12,5),FlatStyle=FlatStyle.Flat,BackColor=primary?Coral:Color.White,ForeColor=primary?Color.White:Ink,Margin=new Padding(4,3,6,3),UseVisualStyleBackColor=false};
      b.FlatAppearance.BorderColor=primary?Coral:Color.FromArgb(207,217,222);b.Click+=click;return b;
    }
    static TableLayoutPanel Grid(int columns) {var t=new TableLayoutPanel{AutoSize=true,Dock=DockStyle.Top,ColumnCount=columns,Padding=new Padding(12),BackColor=Color.White};for(int i=0;i<columns;++i)t.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,100f/columns));return t;}
    static void Row(TableLayoutPanel panel,string label,Control value) {int r=panel.RowCount++;panel.RowStyles.Add(new RowStyle(SizeType.AutoSize));panel.Controls.Add(TextLabel(label),0,r);panel.Controls.Add(value,1,r);}
    static FlowLayoutPanel Flow(params Control[] controls) {var p=new BufferedFlow{AutoSize=true,Dock=DockStyle.Top,WrapContents=true,Padding=new Padding(8)};p.Controls.AddRange(controls);return p;}
    static GroupBox Group(string title,Control content) {var g=new GroupBox{Text=title,AutoSize=true,Dock=DockStyle.Top,Padding=new Padding(10),Margin=new Padding(0,6,0,10)};g.Controls.Add(content);return g;}
    public MainForm() {
      Text="Coral-Wave Control";Font=new Font("Microsoft YaHei UI",10);ForeColor=Ink;BackColor=Color.FromArgb(244,247,248);
      var iconAsset=GetType().Assembly.GetManifestResourceStream("CoralWave.Icon.ico");if(iconAsset!=null)Icon=new Icon(iconAsset);
      ClientSize=new Size(1080,870);MinimumSize=new Size(1000,780);AutoScaleMode=AutoScaleMode.Dpi;StartPosition=FormStartPosition.CenterScreen;
      var root=new TableLayoutPanel{Dock=DockStyle.Fill,ColumnCount=1,RowCount=5,Padding=new Padding(22,14,22,12)};
      root.RowStyles.Add(new RowStyle(SizeType.Absolute,76));root.RowStyles.Add(new RowStyle(SizeType.Absolute,54));root.RowStyles.Add(new RowStyle(SizeType.Percent,100));root.RowStyles.Add(new RowStyle(SizeType.AutoSize));root.RowStyles.Add(new RowStyle(SizeType.AutoSize));Controls.Add(root);
      var heading=new TableLayoutPanel{Dock=DockStyle.Fill,ColumnCount=2};heading.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute,330));heading.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,100));
      var logo=new PictureBox{Size=new Size(64,64),SizeMode=PictureBoxSizeMode.Zoom,Margin=new Padding(4,1,14,1)};
      var asset=GetType().Assembly.GetManifestResourceStream("CoralWave.Logo.png");if(asset!=null)logo.Image=Image.FromStream(asset);
      var brand=new FlowLayoutPanel{Dock=DockStyle.Fill,WrapContents=false,Padding=new Padding(0)};brand.Controls.Add(logo);
      brand.Controls.Add(new Label{Text="Coral-Wave",AutoSize=true,Font=new Font("Segoe UI",21,FontStyle.Bold),ForeColor=Ink,Margin=new Padding(0,11,0,0)});
      heading.Controls.Add(brand,0,0);heading.Controls.Add(new Label{Text="USB 声卡控制台  ·  MCP\nSAMD21E18A  +  WM8978",Dock=DockStyle.Fill,TextAlign=ContentAlignment.MiddleRight,ForeColor=Muted},1,0);root.Controls.Add(heading,0,0);
      devices.DropDownStyle=ComboBoxStyle.DropDownList;devices.Width=490;
      devices.SelectedIndexChanged+=delegate {var d=devices.SelectedItem as DeviceEntry;if(d!=null&&d.Serial!=selectedSerial){selected=d;selectedSerial=d.Serial;connected=false;dirty=false;}};
      connection.AutoSize=true;connection.Padding=new Padding(10,7,0,0);connection.Text="正在查找 USB 设备…";
      root.Controls.Add(Flow(devices,connection),0,1);
      tabs.Dock=DockStyle.Fill;root.Controls.Add(tabs,0,2);
      var playback=Page("耳机与音色");var record=Page("麦克风");var update=Page("USB 固件更新");
      var output=Grid(2);Row(output,"耳机模拟增益 · dB",headphone);Row(output,"DAC 数字余量 · dB",preamp);Row(output,"立体声扩展 · %",depth);
      Row(output,"DAC 限幅",Flow(limiter,limitLevel));Row(output,"转换器",osr);
      playback.Controls.Add(Group("输出",output));
      var eq=Grid(5);string[][] f={new[]{"80 Hz","105 Hz","135 Hz","175 Hz"},new[]{"230 Hz","300 Hz","385 Hz","500 Hz"},new[]{"650 Hz","850 Hz","1.1 kHz","1.4 kHz"},new[]{"1.8 kHz","2.4 kHz","3.2 kHz","4.1 kHz"},new[]{"5.3 kHz","6.9 kHz","9 kHz","11.7 kHz"}};
      for(int i=0;i<5;++i){var band=new FlowLayoutPanel{AutoSize=true,FlowDirection=FlowDirection.TopDown,WrapContents=false,Dock=DockStyle.Fill};band.Controls.Add(TextLabel("频段 "+(i+1)+(i==0?" · 低架":i==4?" · 高架":" · 峰值")));
        frequencies[i]=Choice(f[i]);frequencies[i].Width=146;gains[i]=Number(-12,12,1,0);band.Controls.Add(frequencies[i]);band.Controls.Add(TextLabel("增益 dB"));band.Controls.Add(gains[i]);
        if(i>0&&i<4){widths[i-1]=Check("宽带宽");band.Controls.Add(widths[i-1]);}eq.Controls.Add(band,i,0);}
      var eqContainer=new Panel{AutoSize=true,Dock=DockStyle.Top};eqContainer.Controls.Add(Flow(TextLabel("EQ 与 3D 固定用于播放。提升频段时，建议给 DAC 留出余量。"),ButtonOf("建议余量",delegate {decimal g=Math.Max(0,gains.Max(x=>x.Value));preamp.Value=-Math.Min(24,g+(depth.Value>0?6:0));})));eqContainer.Controls.Add(eq);
      playback.Controls.Add(Group("WM8978 五段均衡器",eqContainer));
      preset=Choice("原声 / 平直","温和低频","人声清晰");playback.Controls.Add(Flow(TextLabel("起点预设"),preset,ButtonOf("载入到界面",delegate {var p=new Parameters();if(preset.SelectedIndex==1){p.EqGain=new[]{3,1,0,-1,1};p.DacAttenuation=8;}if(preset.SelectedIndex==2){p.EqGain=new[]{-2,-1,2,2,0};p.DacAttenuation=6;}LoadParameters(p);dirty=true;++editRevision;ScheduleLive();})));
      playback.Controls.Add(TextLabel("数值只改变音色和增益，无法修复焊接、底噪或失真的硬件问题。Windows 的系统音量仍然独立有效。"));
      var input=Grid(2);Row(input,"麦克风 PGA · dB",mic);Row(input,"输入提升",boost);Row(input,"高通滤波",hpf);Row(input,"语音模式截止频率 · 48 kHz",hpfCut);
      Row(input,"电平控制",alc);Row(input,"ALC 目标 · dBFS",alcTarget);Row(input,"噪声门",gate);Row(input,"噪声门门限",gateLevel);
      record.Controls.Add(Group("板载单声道麦克风",input));record.Controls.Add(TextLabel("ALC 使用左通道，增益范围为 -12 至 +23.25 dB。启用后，PGA 数值是起始增益。"));
      record.Controls.Add(TextLabel("噪声门用于抑制 ALC 在安静时不断提高增益，不是数字降噪。麦克风太响或削顶时，先降低 PGA 或关闭 +20 dB。"));
      packageLabel.AutoSize=true;packageLabel.MaximumSize=new Size(880,0);packageLabel.Text="请选择 .cwfw 固件包。程序会先检查型号、分区地址和 CRC。";
      flash=ButtonOf("通过 USB 烧录",async delegate {await Upload();},true);
      update.Controls.Add(Group("固件包",Flow(ButtonOf("选择固件…",delegate {ChooseFirmware();}),packageLabel)));
      update.Controls.Add(Flow(flash,ButtonOf("等待拔插并恢复烧录",async delegate {await Recover();}),ButtonOf("运行已安装固件",async delegate {await RunInstalled();})));
      progress.Dock=DockStyle.Top;progress.Height=22;progress.Margin=new Padding(15);update.Controls.Add(progress);
      log.Multiline=true;log.ReadOnly=true;log.ScrollBars=ScrollBars.Vertical;log.Height=195;log.Dock=DockStyle.Top;log.BackColor=Color.White;log.Font=new Font("Consolas",9);update.Controls.Add(log);
      update.Controls.Add(TextLabel("烧录时声卡会短暂断开，完成后自动恢复。首次安装引导程序需要 J-Link，之后使用板子的 USB 即可。"));
      update.Controls.Add(TextLabel("若写入被打断，重新插入 USB 后可以再烧录。固件不能升级引导区，也不能修改芯片熔丝。"));
      var ai=Page("AI / MCP 接入");
      ai.Controls.Add(TextLabel("把本地 MCP 服务接到 AI 客户端，即可查询板子、调整音色和通过 USB 更新固件。无需安装 Python 或 Node.js。"));
      string cliPath=Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"Coral-Wave-CLI.exe");
      string mcpJson="{\r\n  \"mcpServers\": {\r\n    \"coral-wave\": {\r\n      \"command\": "+new JavaScriptSerializer().Serialize(cliPath)+",\r\n      \"args\": [\"--mcp\"]\r\n    }\r\n  }\r\n}";
      var configBox=new TextBox{Text=mcpJson,Multiline=true,ReadOnly=true,ScrollBars=ScrollBars.Both,WordWrap=false,Height=215,Dock=DockStyle.Top,Font=new Font("Consolas",10),BackColor=Color.White};
      ai.Controls.Add(Group("本地 stdio 服务配置",configBox));ai.Controls.Add(Flow(ButtonOf("复制 MCP 配置",delegate {Clipboard.SetText(mcpJson);message.Text="MCP 配置已复制。";}),ButtonOf("复制启动命令",delegate {Clipboard.SetText("\""+cliPath+"\" --mcp");message.Text="MCP 启动命令已复制。";})));
      ai.Controls.Add(TextLabel("支持 mcpServers JSON 的客户端可直接使用这段配置。其他客户端分别填写 command 和 args，传输方式选 stdio。"));
      ai.Controls.Add(TextLabel("常用工具\ncoral_list_devices / coral_get_status / coral_get_audio\ncoral_set_audio / coral_save_to_board\ncoral_validate_firmware / coral_flash_firmware / coral_recover_firmware"));
      ai.Controls.Add(TextLabel("调参使用 dB、Hz 等单位，范围由固件校验。烧录会短暂中断声卡；客户端可按工具标记处理确认。"));
      // Top-down flow pages use a single column so text and controls remain usable at higher DPI.
      apply=ButtonOf("应用参数",async delegate {await ApplyParameters();},true);read=ButtonOf("读取当前设置",async delegate {await ReadParameters();});
      saveDevice=ButtonOf("保存到板子",async delegate {await SaveToBoard();});
      var toolbar=Flow(apply,read,saveDevice,ButtonOf("导出预设…",delegate {ExportPreset();}),ButtonOf("导入预设…",delegate {ImportPreset();}),live);
      toolbar.MinimumSize=new Size(0,58);toolbar.Padding=new Padding(4,8,4,8);toolbar.Margin=new Padding(0,5,0,0);live.Margin=new Padding(8,10,4,3);root.Controls.Add(toolbar,0,3);
      var footer=new TableLayoutPanel{AutoSize=true,Dock=DockStyle.Top,ColumnCount=1,RowCount=2,Padding=new Padding(4,3,4,4),Margin=new Padding(0)};
      footer.RowStyles.Add(new RowStyle(SizeType.AutoSize));footer.RowStyles.Add(new RowStyle(SizeType.AutoSize));
      message.Text="参数修改后点击应用，或勾选自动应用。";message.AutoSize=true;message.Margin=new Padding(0,2,0,5);
      detail.AutoSize=true;detail.ForeColor=Muted;detail.Margin=new Padding(0,0,0,2);
      footer.SizeChanged+=delegate {message.MaximumSize=new Size(Math.Max(100,footer.ClientSize.Width-12),0);detail.MaximumSize=message.MaximumSize;};
      footer.Controls.Add(message,0,0);footer.Controls.Add(detail,0,1);root.Controls.Add(footer,0,4);
      LoadParameters(new Parameters());WatchEdits(this);
      hpf.SelectedIndexChanged+=delegate {hpfCut.Enabled=hpf.SelectedIndex==2;};
      alc.CheckedChanged+=delegate {if(!alc.Checked)gate.Checked=false;mic.Enabled=!alc.Checked;alcTarget.Enabled=alc.Checked;gate.Enabled=alc.Checked;gateLevel.Enabled=alc.Checked&&gate.Checked;};
      gate.CheckedChanged+=delegate {gateLevel.Enabled=alc.Checked&&gate.Checked;};
      live.CheckedChanged+=delegate {if(live.Checked)ScheduleLive();else debounce.Stop();};
      timer.Interval=1200;timer.Tick+=async delegate {await Scan();};timer.Start();
      debounce.Interval=450;debounce.Tick+=async delegate {debounce.Stop();if(dirty&&live.Checked&&!busy)await ApplyParameters();};
      FormClosing+=delegate(object sender,FormClosingEventArgs e){if(busy){e.Cancel=true;message.Text="操作进行中，请等待结束后关闭。";}else{closing=true;timer.Stop();debounce.Stop();}};
      Shown+=async delegate {await Scan();};
    }
    FlowLayoutPanel Page(string name) {var tab=new TabPage(name){BackColor=BackColor};var p=new AudioPage();tab.Controls.Add(p);tabs.TabPages.Add(tab);return p;}
    void WatchEdits(Control c) {
      var n=c as NumericUpDown;if(n!=null)n.ValueChanged+=Changed;
      var combo=c as ComboBox;if(combo!=null&&combo!=devices&&combo!=preset)combo.SelectedIndexChanged+=Changed;
      var cb=c as CheckBox;if(cb!=null&&cb!=live)cb.CheckedChanged+=Changed;
      foreach(Control child in c.Controls)WatchEdits(child);
    }
    void Changed(object sender,EventArgs e) {if(loading)return;dirty=true;++editRevision;message.Text="界面有修改，尚未应用到板子。";ScheduleLive();}
    void ScheduleLive() {if(live.Checked){debounce.Stop();debounce.Start();}}
    Parameters FromControls() {
      var p=new Parameters{Headphone=(int)(headphone.Value+57),MicPga=(int)((mic.Value+12)/.75m),MicBoost=boost.Checked?1:0,
        Hpf=hpf.SelectedIndex,HpfCut=hpfCut.SelectedIndex,Oversample=osr.Checked?1:0,Depth3d=(int)Math.Round(depth.Value*15/100),
        Limiter=limiter.Checked?1:0,LimiterLevel=limitLevel.SelectedIndex,Alc=alc.Checked?1:0,AlcTarget=(int)((alcTarget.Value+22.5m)/1.5m),
        NoiseGate=gate.Checked?1:0,GateThreshold=gateLevel.SelectedIndex,DacAttenuation=(int)(-preamp.Value*2)};
      for(int i=0;i<5;++i){p.EqGain[i]=(int)gains[i].Value;p.EqFrequency[i]=frequencies[i].SelectedIndex;}
      for(int i=0;i<3;++i)if(widths[i].Checked)p.EqWide|=1<<i;p.Validate();return p;
    }
    void LoadParameters(Parameters p) {
      loading=true;tabs.SuspendLayout();SuspendLayout();try {
        headphone.Value=p.Headphone-57;mic.Value=p.MicPga*.75m-12;boost.Checked=p.MicBoost!=0;hpf.SelectedIndex=p.Hpf;hpfCut.SelectedIndex=p.HpfCut;
        hpfCut.Enabled=p.Hpf==2;osr.Checked=p.Oversample!=0;depth.Value=p.Depth3d*100m/15;limiter.Checked=p.Limiter!=0;limitLevel.SelectedIndex=p.LimiterLevel;
        alc.Checked=p.Alc!=0;alcTarget.Value=p.AlcTarget*1.5m-22.5m;gate.Checked=p.NoiseGate!=0;gateLevel.SelectedIndex=p.GateThreshold;preamp.Value=-p.DacAttenuation*.5m;
        mic.Enabled=p.Alc==0;alcTarget.Enabled=p.Alc!=0;gate.Enabled=p.Alc!=0;gateLevel.Enabled=p.Alc!=0&&p.NoiseGate!=0;
        for(int i=0;i<5;++i){gains[i].Value=p.EqGain[i];frequencies[i].SelectedIndex=p.EqFrequency[i];}
        for(int i=0;i<3;++i)widths[i].Checked=(p.EqWide&(1<<i))!=0;
      }finally{ResumeLayout(true);tabs.ResumeLayout(true);loading=false;}
    }
    async Task Work(Func<Task> work) {
      if(busy)return;busy=true;timer.Stop();debounce.Stop();while(scanning)await Task.Delay(20);devices.Enabled=false;tabs.Enabled=false;apply.Enabled=read.Enabled=saveDevice.Enabled=false;
      try {await work();}catch(Exception ex){live.Checked=false;message.ForeColor=Color.Firebrick;message.Text=ex.Message;Append("失败  "+ex.Message);}
      finally {busy=false;devices.Enabled=tabs.Enabled=true;apply.Enabled=read.Enabled=saveDevice.Enabled=connected&&selected!=null&&!selected.Bootloader;if(!closing)timer.Start();ScheduleLive();}
    }
    async Task Scan() {
      if(busy||scanning||closing)return;scanning=true;
      try {
        var list=await Task.Run(()=>Device.Find());if(closing)return;
        var e=list.FirstOrDefault(x=>x.Serial==selectedSerial)??list.FirstOrDefault();
        if(e==null){connected=false;connection.Text="未连接";detail.Text="请接板子的 USB-C。J-Link 不参与上位机通信。";apply.Enabled=read.Enabled=saveDevice.Enabled=false;return;}
        bool fresh=!connected||selected==null||selected.Path!=e.Path;
        if(list.Count!=devices.Items.Count||fresh){devices.Items.Clear();foreach(var item in list)devices.Items.Add(item);devices.SelectedItem=e;}
        selected=e;selectedSerial=e.Serial;
        var data=await Task.Run(()=>{using(var d=new Device(e)){var info=d.GetInfo();info.Validate(e.Bootloader);return Tuple.Create(info,e.Bootloader?null:d.Status(),!dirty&&!e.Bootloader?d.GetParameters():null);}});
        connected=true;apply.Enabled=read.Enabled=saveDevice.Enabled=!e.Bootloader;
        connection.Text=e.Bootloader?"USB 恢复模式":"已连接  ·  固件 "+data.Item1.VersionText;connection.ForeColor=e.Bootloader?Coral:Color.FromArgb(32,133,103);
        if(data.Item3!=null)LoadParameters(data.Item3);
        if(data.Item2!=null){var s=data.Item2;detail.Text="48 kHz / 16 bit  ·  立体声播放 / 单声道采集  ·  WM8978 "+(s[3]==1?"ACK":"未应答")+"  ·  DMA 错误 "+s[8]+"  ·  运行 "+s[0]/1000+" s";}
        else detail.Text="引导程序已连接。可在 USB 固件更新页烧录，或运行已安装固件。";
      }catch(Exception ex){connected=false;connection.Text="连接暂不可用";detail.Text=ex.Message;}
      finally {scanning=false;}
    }
    DeviceEntry RequireDevice() {if(!connected||selected==null)throw new IOException("请先连接板子的 USB-C");return selected;}
    async Task ApplyParameters() {
      if(busy)return;
      await Work(async ()=>{var e=RequireDevice();var p=FromControls();long revision=editRevision;
        var applied=await Task.Run(()=>{using(var d=new Device(e))return d.Apply(p);});
        if(editRevision==revision){LoadParameters(applied);dirty=false;}
        message.ForeColor=Ink;message.Text="参数已写入 WM8978。断电前需要保留时，请保存到板子。";
      });
    }
    async Task ReadParameters() {await Work(async ()=>{var e=RequireDevice();var p=await Task.Run(()=>{using(var d=new Device(e))return d.GetParameters();});LoadParameters(p);dirty=false;message.Text="已读取固件保存的设置镜像。WM8978 控制口不支持寄存器读回。";message.ForeColor=Ink;});}
    async Task SaveToBoard() {
      await Work(async ()=>{var e=RequireDevice();if(dirty)throw new IOException("请先应用参数，再保存到板子。");
        await Task.Run(()=>{using(var d=new Device(e))d.Command(5);System.Threading.Thread.Sleep(1200);Device.WaitFor(e.Serial,false,15000,false);});
        message.ForeColor=Ink;message.Text="参数已保存，板子重启后恢复设置。";connected=false;
      });
    }
    void ChooseFirmware() {
      using(var dialog=new OpenFileDialog{Filter="Coral-Wave 固件 (*.cwfw)|*.cwfw",InitialDirectory=AppDomain.CurrentDomain.BaseDirectory})
      if(dialog.ShowDialog()==DialogResult.OK)try {image=FirmwareImage.Load(dialog.FileName);imagePath=dialog.FileName;packageLabel.Text=Path.GetFileName(imagePath)+"  ·  v"+image.VersionText+"  ·  "+image.Data.Length+" 字节\nCRC32 "+image.Crc.ToString("X8")+"  ·  SAMD21E18A";}
      catch(Exception ex){image=null;message.Text=ex.Message;}
    }
    async Task Upload() {
      if(image==null){ChooseFirmware();if(image==null)return;}
      await Work(async ()=>{var e=RequireDevice();var current=FirmwareImage.Load(imagePath);progress.Value=0;Append("开始 USB 升级  v"+current.VersionText);
        var reporter=new Progress<Tuple<int,string>>(p=>{progress.Value=p.Item1;if(p.Item1==0||p.Item1>=94)Append(p.Item2);else message.Text=p.Item2;});
        selected=await Task.Run(()=>current.Upload(e,(n,s)=>((IProgress<Tuple<int,string>>)reporter).Report(Tuple.Create(n,s))));
        connected=false;dirty=false;message.ForeColor=Ink;message.Text="USB 烧录及 Flash CRC 校验完成，声卡已重新连接。";
      });
    }
    async Task RunInstalled() {await Work(async ()=>{var e=RequireDevice();if(!e.Bootloader)throw new IOException("声卡固件已在运行。");await Task.Run(()=>{using(var d=new Device(e))d.Command(20);Device.WaitFor(e.Serial,false,15000,false);});connected=false;});}
    async Task Recover() {
      if(image==null){ChooseFirmware();if(image==null)return;}
      await Work(async ()=>{var current=FirmwareImage.Load(imagePath);message.Text="正在等待 45 秒，请拔掉再插入板子的 USB-C。";Append(message.Text);progress.Value=0;
        var reporter=new Progress<Tuple<int,string>>(p=>{progress.Value=p.Item1;message.Text=p.Item2;});
        selected=await Task.Run(()=>{var boot=Device.WaitRecovery(selectedSerial,45000);return current.Upload(boot,(n,s)=>((IProgress<Tuple<int,string>>)reporter).Report(Tuple.Create(n,s)));});
        connected=false;dirty=false;message.ForeColor=Ink;message.Text="恢复烧录完成，声卡已重新连接。";
      });
    }
    void ExportPreset() {
      try {var p=FromControls();using(var dialog=new SaveFileDialog{Filter="Coral-Wave 预设 (*.json)|*.json",FileName="Coral-Wave-preset.json"})if(dialog.ShowDialog()==DialogResult.OK){File.WriteAllText(dialog.FileName,new JavaScriptSerializer().Serialize(p));message.Text="预设已导出到电脑。";}}
      catch(Exception ex){message.Text=ex.Message;}
    }
    void ImportPreset() {
      using(var dialog=new OpenFileDialog{Filter="Coral-Wave 预设 (*.json)|*.json"})if(dialog.ShowDialog()==DialogResult.OK)
      try {if(new FileInfo(dialog.FileName).Length>16384)throw new IOException("预设文件过大");var p=new JavaScriptSerializer().Deserialize<Parameters>(File.ReadAllText(dialog.FileName));if(p==null)throw new IOException("空预设");p.Validate();LoadParameters(p);dirty=true;++editRevision;message.Text="预设已载入界面，应用后生效。";ScheduleLive();}
      catch(Exception ex){message.Text="预设未载入  "+ex.Message;}
    }
    void Append(string text) {log.AppendText(DateTime.Now.ToString("HH:mm:ss")+"  "+text+Environment.NewLine);}
    public void RenderPreview(string path,int page=0) {Show();tabs.SelectedIndex=page;Application.DoEvents();using(var bitmap=new Bitmap(Width,Height)){DrawToBitmap(bitmap,new Rectangle(0,0,Width,Height));bitmap.Save(path);}Close();}
    public object TestLayout() {
      timer.Stop();debounce.Stop();Show();int switches=0;double max=0,total=0;bool settled=true;
      foreach(Size size in new[]{new Size(1000,780),new Size(1080,870),new Size(1840,970)}) {
        ClientSize=size;Application.DoEvents();
        for(int n=0;n<16;++n){
          var watch=System.Diagnostics.Stopwatch.StartNew();tabs.SelectedIndex=n%4;Application.DoEvents();tabs.Refresh();Update();watch.Stop();
          max=Math.Max(max,watch.Elapsed.TotalMilliseconds);total+=watch.Elapsed.TotalMilliseconds;++switches;
          var page=(AudioPage)tabs.SelectedTab.Controls[0];int layouts=0;LayoutEventHandler counted=delegate{++layouts;};page.Layout+=counted;
          Application.DoEvents();Application.DoEvents();page.Layout-=counted;if(layouts>0)settled=false;
        }
      }
      Close();if(!settled||max>750)throw new IOException("界面布局未稳定，或切页耗时过长。");
      return new{passed=true,switches=switches,maxSwitchMs=Math.Round(max,2),averageSwitchMs=Math.Round(total/switches,2),layoutSettled=settled};
    }
  }
}
