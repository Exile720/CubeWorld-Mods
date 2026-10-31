using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Text;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;
using System.Web.Script.Serialization;
using System.Windows.Forms;

namespace CubeWorldModManager
{
    internal static class Program
    {
        [DllImport("user32.dll")] private static extern bool SetProcessDPIAware();
        [STAThread] private static void Main(string[] args)
        {
            try
            {
                SetProcessDPIAware();
                Application.EnableVisualStyles();
                Application.SetCompatibleTextRenderingDefault(false);
                string root = Path.GetFullPath(Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "..", ".."));
                for (int i = 0; i < args.Length - 1; i++) if (args[i] == "--root") root = Path.GetFullPath(args[i + 1]);
                if (!File.Exists(Path.Combine(root, "Manage-Mod.ps1"))) throw new Exception("没有找到模组管理文件。请从项目目录中的入口打开。");
                Application.Run(new ManagerForm(root));
            }
            catch (Exception ex) { MessageBox.Show(ex.Message, "魔方世界 · 模组管理器", MessageBoxButtons.OK, MessageBoxIcon.Warning); }
        }
    }

    public sealed class ModState
    {
        public string id { get; set; }
        public string stem { get; set; }
        public string name { get; set; }
        public string description { get; set; }
        public bool installed { get; set; }
        public bool? enabled { get; set; }
        public bool canToggle { get; set; }
        public string statusLabel { get; set; }
        public string reason { get; set; }
    }
    public sealed class ManagerState
    {
        public string target { get; set; }
        public string targetLabel { get; set; }
        public string gameDirectory { get; set; }
        public bool directoryExists { get; set; }
        public bool canManage { get; set; }
        public string blockReason { get; set; }
        public bool gameRunning { get; set; }
        public bool processCheckOk { get; set; }
        public bool canLaunch { get; set; }
        public string launchReason { get; set; }
        public ModState[] mods { get; set; }
    }
    public sealed class LaunchResult
    {
        public bool requested { get; set; }
        public string target { get; set; }
        public int processId { get; set; }
    }

    internal static class Skin
    {
        public static readonly Color Background = Color.FromArgb(20, 34, 47);
        public static readonly Color Panel = Color.FromArgb(36, 57, 77);
        public static readonly Color Inset = Color.FromArgb(26, 44, 60);
        public static readonly Color Edge = Color.FromArgb(76, 100, 116);
        public static readonly Color Text = Color.FromArgb(229, 235, 238);
        public static readonly Color Muted = Color.FromArgb(174, 193, 201);
        public static readonly Color Gold = Color.FromArgb(232, 199, 118);
        public static readonly Color Green = Color.FromArgb(146, 200, 124);
        public static readonly Color Amber = Color.FromArgb(239, 179, 113);
        public static float Scale = 1;
        private static PrivateFontCollection privateFonts;
        private static FontFamily family;
        private static FontFamily bodyFamily;
        [DllImport("gdi32.dll",CharSet=CharSet.Unicode)] private static extern int AddFontResourceEx(string file,uint flags,IntPtr reserved);
        public static void LoadFont(string root)
        {
            family = new FontFamily("Microsoft YaHei UI");
            bodyFamily = family;
            string font = Path.Combine(root, "playtest", "resource1.dat");
            if (!File.Exists(font)) return;
            try
            {
                privateFonts = new PrivateFontCollection();
                privateFonts.AddFontFile(font);
                if (privateFonts.Families.Length > 0 && AddFontResourceEx(font,0x10,IntPtr.Zero)>0) family = privateFonts.Families[0];
            }
            catch { /* The installed game's font is optional; no resource is copied. */ }
        }
        public static Font Font(float size) { return new Font(size>=20 ? family : bodyFamily, size * Scale, FontStyle.Regular, GraphicsUnit.Pixel); }
        public static void TextAt(Graphics g, string text, float size, Color color, Rectangle bounds, TextFormatFlags flags)
        {
            using (Font font = Font(size)) TextRenderer.DrawText(g, text ?? "", font, bounds, color, flags | TextFormatFlags.NoPrefix | TextFormatFlags.PreserveGraphicsClipping);
        }
        public static void Frame(Graphics g, Rectangle r, Color fill, Color border)
        {
            float line=Math.Max(1f,Scale);
            using(Brush b=new SolidBrush(fill))g.FillRectangle(b,r);
            using(Pen p=new Pen(border,line))
            {
                p.Alignment=PenAlignment.Inset;g.DrawRectangle(p,r);
            }
        }
        public static void Cube(Graphics g, Rectangle r)
        {
            int x = r.Left, y = r.Top, w = r.Width, h = r.Height, mid = x + w / 2;
            using (Brush b = new SolidBrush(Gold)) g.FillPolygon(b, new Point[] {new Point(x,y+h/4),new Point(mid,y),new Point(x+w,y+h/4),new Point(mid,y+h/2)});
            using (Brush b = new SolidBrush(Color.FromArgb(181,135,56))) g.FillPolygon(b, new Point[] {new Point(x,y+h/4),new Point(mid,y+h/2),new Point(mid,y+h),new Point(x,y+h*3/4)});
            using (Brush b = new SolidBrush(Color.FromArgb(220,166,65))) g.FillPolygon(b, new Point[] {new Point(mid,y+h/2),new Point(x+w,y+h/4),new Point(x+w,y+h*3/4),new Point(mid,y+h)});
        }
    }

    internal sealed class PixelButton : Button
    {
        public bool Selected;
        public bool Quiet;
        private bool hovering;
        public PixelButton()
        {
            SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer, true);
            FlatStyle = FlatStyle.Flat; Cursor = Cursors.Hand; TabStop = true;
        }
        protected override void OnMouseEnter(EventArgs e) { hovering = true; Invalidate(); base.OnMouseEnter(e); }
        protected override void OnMouseLeave(EventArgs e) { hovering = false; Invalidate(); base.OnMouseLeave(e); }
        protected override void OnPaint(PaintEventArgs e)
        {
            Color fill = Selected ? Color.FromArgb(81, 74, 46) : (hovering && Enabled ? Color.FromArgb(49, 75, 94) : Skin.Inset);
            Color border = Selected ? Skin.Gold : (Quiet ? Color.FromArgb(105, 133, 147) : Skin.Edge);
            Skin.Frame(e.Graphics, new Rectangle(0,0,Width-1,Height-1), fill, border);
            Skin.TextAt(e.Graphics, Text, 14, Enabled ? (Selected ? Skin.Gold : Skin.Text) : Skin.Muted, ClientRectangle, TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter | TextFormatFlags.SingleLine);
            if (Focused) { Rectangle focus = ClientRectangle; focus.Inflate(-8,-8); ControlPaint.DrawFocusRectangle(e.Graphics,focus,Skin.Gold,fill); }
        }
    }

    internal sealed class ModToggle : CheckBox
    {
        public bool Unknown;
        public ModToggle()
        {
            Appearance = Appearance.Button; AutoCheck = false; TabStop = true; Cursor = Cursors.Hand;
            SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer, true);
        }
        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            Color color = Unknown ? Skin.Muted : (Checked ? Skin.Green : Color.FromArgb(154,170,178));
            Skin.Frame(g,new Rectangle(0,0,Width-1,Height-1),Checked && !Unknown ? Color.FromArgb(46,75,53) : Skin.Inset,Enabled ? color : Color.FromArgb(102,120,131));
            int pad = Math.Max(3,(int)(4*Skin.Scale));
            int thumb = Height-pad*2;
            Rectangle knob = new Rectangle(Checked && !Unknown ? Width-pad-thumb : pad,pad,thumb,thumb);
            using (Brush b = new SolidBrush(Enabled ? color : Color.FromArgb(115,134,143))) g.FillRectangle(b,knob);
            Rectangle text = new Rectangle(Checked && !Unknown ? 4 : thumb+pad,0,Width-thumb-pad-4,Height);
            Skin.TextAt(g,Unknown ? "?" : (Checked ? "开" : "关"),14,Enabled ? Skin.Text : Skin.Muted,text,TextFormatFlags.HorizontalCenter|TextFormatFlags.VerticalCenter|TextFormatFlags.SingleLine);
            if (Focused) {Rectangle focus=ClientRectangle; focus.Inflate(-3,-3); ControlPaint.DrawFocusRectangle(g,focus,Skin.Gold,Skin.Inset);}
        }
    }

    internal sealed class ModRow : Panel
    {
        public readonly string Id;
        public readonly ModToggle Toggle;
        public ModState State;
        public bool Working;
        public bool Locked;
        private readonly string title, description;
        public ModRow(string id,string titleText,string descriptionText)
        {
            Id=id; title=titleText; description=descriptionText;
            DoubleBuffered=true; BackColor=Skin.Panel;
            AccessibleName=titleText;AccessibleDescription=descriptionText;
            Toggle=new ModToggle(); Toggle.AccessibleName=titleText+"开关"; Controls.Add(Toggle);
        }
        protected override void OnResize(EventArgs e)
        {
            base.OnResize(e);
            if(Toggle!=null) Toggle.Bounds=new Rectangle(Width-(int)(88*Skin.Scale),(Height-(int)(30*Skin.Scale))/2,(int)(72*Skin.Scale),(int)(30*Skin.Scale));
        }
        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g=e.Graphics; float s=Skin.Scale;
            Rectangle icon=new Rectangle((int)(14*s),(int)(23*s),(int)(42*s),(int)(42*s));
            using(Brush tile=new SolidBrush(Skin.Inset))g.FillRectangle(tile,icon);
            int x=icon.Left+(int)(8*s),y=icon.Top+(int)(6*s),u=Math.Max(1,(int)Math.Round(2.5*s));
            if (Id=="Navigation")
            {
                using(Brush b=new SolidBrush(Skin.Text)) {g.FillRectangle(b,x,y+u,3*u,8*u);g.FillRectangle(b,x+3*u,y,3*u,8*u);g.FillRectangle(b,x+6*u,y+u,3*u,8*u);}
                using(Brush b=new SolidBrush(Color.FromArgb(110,151,138))) {g.FillRectangle(b,x+u,y+3*u,2*u,2*u);g.FillRectangle(b,x+4*u,y+2*u,u,4*u);}
                using(Brush b=new SolidBrush(Skin.Gold)) g.FillPolygon(b,new Point[]{new Point(x+4*u,y+3*u),new Point(x+8*u,y+3*u),new Point(x+6*u,y+7*u)});
            }
            else if (Id=="RegionExplorer")
            {
                using(Brush b=new SolidBrush(Skin.Text)) g.FillPolygon(b,new Point[]{new Point(x+5*u,y),new Point(x+10*u,y+5*u),new Point(x+5*u,y+10*u),new Point(x,y+5*u)});
                using(Brush b=new SolidBrush(Skin.Gold)) g.FillPolygon(b,new Point[]{new Point(x+5*u,y+u),new Point(x+7*u,y+6*u),new Point(x+5*u,y+5*u),new Point(x+3*u,y+6*u)});
                using(Brush b=new SolidBrush(Skin.Inset)) g.FillRectangle(b,x+4*u,y+5*u,2*u,3*u);
            }
            else if (Id=="MapLight")
            {
                using(Brush b=new SolidBrush(Skin.Gold)){g.FillRectangle(b,x+2*u,y+u,6*u,8*u);g.FillRectangle(b,x+u,y+2*u,8*u,6*u);}
                using(Brush b=new SolidBrush(Skin.Inset)){g.FillRectangle(b,x+5*u,y,5*u,5*u);g.FillRectangle(b,x+7*u,y+5*u,3*u,2*u);}
                using(Brush b=new SolidBrush(Skin.Text)) g.FillRectangle(b,x+9*u,y+u,u,u);
            }
            else if (Id=="FastTravel")
            {
                using(Brush b=new SolidBrush(Skin.Gold)) {g.FillRectangle(b,x+4*u,y,2*u,13*u);g.FillRectangle(b,x-u,y+u,11*u,4*u);}
                using(Brush b=new SolidBrush(Skin.Text)) {g.FillRectangle(b,x+6*u,y+2*u,3*u,2*u);g.FillRectangle(b,x+u,y+10*u,9*u,3*u);}
            }
            else if (Id=="BossDraw")
            {
                using(Brush b=new SolidBrush(Skin.Text)) g.FillRectangle(b,x+u,y,8*u,11*u);
                using(Brush b=new SolidBrush(Skin.Gold)) g.FillRectangle(b,x+3*u,y+2*u,7*u,10*u);
                using(Brush b=new SolidBrush(Skin.Inset)) g.FillRectangle(b,x+4*u,y+3*u,5*u,8*u);
                using(Brush b=new SolidBrush(Skin.Gold)){g.FillRectangle(b,x+6*u,y+5*u,u,4*u);g.FillRectangle(b,x+5*u,y+6*u,3*u,u);}
            }
            else if (Id=="InventorySort")
            {
                using(Brush b=new SolidBrush(Skin.Text)) {g.FillRectangle(b,x+2*u,y,6*u,2*u);g.FillRectangle(b,x,y+2*u,10*u,8*u);}
                using(Brush b=new SolidBrush(Skin.Inset)) g.FillRectangle(b,x+u,y+3*u,8*u,6*u);
                using(Brush b=new SolidBrush(Skin.Gold)) {g.FillRectangle(b,x+2*u,y+4*u,2*u,2*u);g.FillRectangle(b,x+6*u,y+4*u,2*u,2*u);g.FillRectangle(b,x+2*u,y+7*u,2*u,u);g.FillRectangle(b,x+6*u,y+7*u,2*u,u);}
            }
            else
            {
                using(Brush b=new SolidBrush(Skin.Text)) {g.FillRectangle(b,x+3*u,y,4*u,3*u);g.FillRectangle(b,x+u,y+3*u,8*u,7*u);}
                using(Brush b=new SolidBrush(Color.FromArgb(195,87,86))) g.FillRectangle(b,x+2*u,y+5*u,6*u,4*u);
                using(Brush b=new SolidBrush(Skin.Gold)) g.FillRectangle(b,x+3*u,y-u,4*u,2*u);
                using(Brush b=new SolidBrush(Color.FromArgb(255,206,174))) g.FillRectangle(b,x+3*u,y+5*u,u,2*u);
            }
            int textX=(int)(72*s), available=Width-textX-(int)(104*s);
            bool showReason=State==null || !State.installed || !State.enabled.HasValue || Locked || !String.IsNullOrEmpty(State.reason);
            Skin.TextAt(g,title,17,Skin.Text,new Rectangle(textX,(int)((showReason ? 7 : 18)*s),available,(int)(23*s)),TextFormatFlags.SingleLine|TextFormatFlags.VerticalCenter|TextFormatFlags.EndEllipsis);
            Skin.TextAt(g,description,13,Skin.Muted,new Rectangle(textX,(int)((showReason ? 31 : 43)*s),available,(int)(20*s)),TextFormatFlags.SingleLine|TextFormatFlags.VerticalCenter|TextFormatFlags.EndEllipsis);
            string status=Working ? "正在读取状态…" : (State==null ? "暂未读取到状态" : State.statusLabel);
            Color statusColor=State!=null && State.installed && State.enabled==true ? Skin.Green : Skin.Muted;
            if(State!=null && !String.IsNullOrEmpty(State.reason)) {status=Locked ? State.statusLabel+"，当前不能切换" : State.reason;statusColor=Skin.Amber;}
            if(showReason)Skin.TextAt(g,status,12,statusColor,new Rectangle(textX,(int)(53*s),available,(int)(30*s)),TextFormatFlags.WordBreak|TextFormatFlags.VerticalCenter|TextFormatFlags.EndEllipsis);
            using(Pen divider=new Pen(Color.FromArgb(53,77,95)))g.DrawLine(divider,(int)(14*s),Height-1,Width-(int)(14*s),Height-1);
        }
        public void Apply(ModState state,bool canWrite,bool busy)
        {
            State=state;Working=busy;Locked=!canWrite;
            Toggle.Unknown=state==null || !state.enabled.HasValue || !state.installed;
            Toggle.Checked=state!=null && state.enabled==true;
            Toggle.Enabled=!busy && canWrite && state!=null && state.canToggle;
            Toggle.AccessibleDescription=state==null ? "正在读取" : state.statusLabel+"。"+state.reason;
            Toggle.Invalidate();Invalidate();
        }
    }

    internal sealed class ManagerForm : Form
    {
        private readonly string root, shell;
        private readonly bool writableShell;
        private string target="Experiment", notice="正在读取模组状态…", feedback="更改会自动备份，下次启动生效。";
        private Color noticeColor=Skin.Muted,feedbackColor=Skin.Muted;
        private ManagerState state;
        private bool busy,writing,launching;
        private readonly ModRow navigation,potion,explorer,mapLight,inventorySort,fastTravel,batchActions,bossDraw;
        private readonly ModRow[] modRows;
        private readonly FlowLayoutPanel modList;
        private readonly PixelButton experiment,formal,refresh,backups,launch,fullscreen;
        private readonly Timer timer;
        private bool layingOutRows,isFullscreen;
        private Rectangle windowedBounds;
        private FormWindowState windowedState;
        private Size windowedMinimumSize;
        private int P(int value) {return (int)Math.Round(value*Skin.Scale);}
        private Rectangle R(int x,int y,int w,int h) {return new Rectangle(P(x),P(y),P(w),P(h));}
        public ManagerForm(string projectRoot)
        {
            root=projectRoot; shell=FindShell(out writableShell); Skin.LoadFont(root);
            using(Stream iconStream=typeof(Program).Assembly.GetManifestResourceStream("CubeWorldModManager.LauncherIcon"))
                Icon=new Icon(iconStream,256,256);
            float dpi;using(Graphics g=Graphics.FromHwnd(IntPtr.Zero)) dpi=g.DpiX/96f;
            Rectangle area=Screen.PrimaryScreen.WorkingArea;
            // Text and controls follow Windows DPI, never the number of Mod rows.
            Skin.Scale=Math.Max(1f,dpi);
            Text="魔方世界 · 模组管理器";AccessibleName=Text;BackColor=Skin.Background;
            AutoScaleMode=AutoScaleMode.None;FormBorderStyle=FormBorderStyle.Sizable;StartPosition=FormStartPosition.CenterScreen;
            MaximizeBox=true;MinimizeBox=true;
            Size frame=SizeFromClientSize(Size.Empty);
            Size available=new Size(Math.Max(320,area.Width-P(48)-frame.Width),Math.Max(320,area.Height-P(48)-frame.Height));
            MinimumSize=SizeFromClientSize(new Size(Math.Min(P(680),available.Width),Math.Min(P(480),available.Height)));
            ClientSize=new Size(Math.Min(P(920),available.Width),Math.Min(P(700),available.Height));
            DoubleBuffered=true;KeyPreview=true;
            fullscreen=new PixelButton();fullscreen.Text="全屏 F11";fullscreen.AccessibleName="切换全屏";fullscreen.Quiet=true;fullscreen.Click+=delegate{ToggleFullscreen();};
            experiment=new PixelButton();experiment.Text="实验副本";experiment.AccessibleName="选择实验副本";experiment.Selected=true;
            formal=new PixelButton();formal.Text="正式游玩";formal.AccessibleName="选择正式游玩副本";
            refresh=new PixelButton();refresh.Text="刷新状态";refresh.AccessibleName="刷新状态";
            backups=new PixelButton();backups.Text="查看备份";backups.AccessibleName="查看备份文件夹";backups.Quiet=true;
            launch=new PixelButton();launch.Text="启动游戏";launch.AccessibleName="启动所选副本的游戏";launch.Selected=true;
            navigation=new ModRow("Navigation","地图与导航","地图标点、城镇名称与店铺指南");
            potion=new ModRow("PotionShop","药水商店","药水随成长更新，始终比可制作的低一档");
            explorer=new ModRow("RegionExplorer","区域探索","提前查看周边城镇、挑战和地标，选点导航");
            mapLight=new ModRow("MapLight","地图照明","夜间大地图自动补光，白天保留原版光照");
            inventorySort=new ModRow("InventorySort","背包分类","胸甲、手套等独立分区，卖出留空不补位");
            fastTravel=new ModRow("FastTravel","旅行驿站","交战也记 Boss，停稳登记落点，F8 传送");
            batchActions=new ModRow("BatchActions","批量补给","点击补给后选数量，A / D 每次减 / 加 10");
            bossDraw=new ModRow("BossDraw","首领抽奖","击败首领额外一抽，F9 开奖，四次职业小保底");
            modRows=new ModRow[]{navigation,potion,explorer,mapLight,inventorySort,fastTravel,batchActions,bossDraw};
            modList=new FlowLayoutPanel();modList.AutoScroll=true;modList.WrapContents=true;modList.FlowDirection=FlowDirection.LeftToRight;
            modList.BackColor=Skin.Panel;modList.AccessibleName="模组列表";
            modList.ClientSizeChanged+=delegate{LayoutModRows();};
            foreach(ModRow row in modRows)
            {
                modList.Controls.Add(row);
                ModRow focusRow=row;row.Toggle.GotFocus+=delegate{modList.ScrollControlIntoView(focusRow);};
            }
            Controls.AddRange(new Control[]{fullscreen,experiment,formal,refresh,backups,launch,modList});
            fullscreen.AccessibleDescription="F11 切换全屏，Esc 返回窗口；标题栏可最大化或还原。";
            experiment.Click+=async delegate{await SelectTarget("Experiment");};
            formal.Click+=async delegate{await SelectTarget("Formal");};
            refresh.Click+=async delegate{await RefreshState(true);};
            backups.Click+=delegate{OpenBackups();};
            launch.Click+=async delegate{await LaunchGame();};
            navigation.Toggle.Click+=async delegate{await ChangeMod(navigation);};
            potion.Toggle.Click+=async delegate{await ChangeMod(potion);};
            explorer.Toggle.Click+=async delegate{await ChangeMod(explorer);};
            mapLight.Toggle.Click+=async delegate{await ChangeMod(mapLight);};
            inventorySort.Toggle.Click+=async delegate{await ChangeMod(inventorySort);};
            fastTravel.Toggle.Click+=async delegate{await ChangeMod(fastTravel);};
            batchActions.Toggle.Click+=async delegate{await ChangeMod(batchActions);};
            bossDraw.Toggle.Click+=async delegate{await ChangeMod(bossDraw);};
            KeyDown+=async delegate(object sender,KeyEventArgs e){if(e.KeyCode==Keys.F5){e.Handled=true;await RefreshState(true);}};
            FormClosing+=delegate(object sender,FormClosingEventArgs e){if(writing){e.Cancel=true;feedback=launching ? "正在交给游戏启动器，请稍等片刻再关闭。" : "更改正在保存，请稍等片刻再关闭。";feedbackColor=Skin.Amber;Invalidate();}};
            Shown+=async delegate{await RefreshState(false);};
            timer=new Timer();timer.Interval=10000;timer.Tick+=async delegate{if(Visible && WindowState!=FormWindowState.Minimized && !busy) await RefreshState(false);};timer.Start();
            FormClosed+=delegate{timer.Stop();timer.Dispose();};
            LayoutControls();
        }
        private void LayoutControls()
        {
            if(modList==null || modRows==null)return;
            int width=(int)(ClientSize.Width/Skin.Scale),height=(int)(ClientSize.Height/Skin.Scale);
            fullscreen.Bounds=R(width-136,20,116,30);
            experiment.Bounds=R(20,70,116,32);formal.Bounds=R(142,70,116,32);
            modList.Bounds=R(20,160,width-40,Math.Max(80,height-246));
            refresh.Bounds=R(20,height-50,108,32);backups.Bounds=R(138,height-50,108,32);
            launch.Bounds=R(width-176,height-50,156,32);
            LayoutModRows();
        }
        private void LayoutModRows()
        {
            if(layingOutRows || modList==null || modRows==null)return;
            layingOutRows=true;
            Point scroll=new Point(-modList.AutoScrollPosition.X,-modList.AutoScrollPosition.Y);
            modList.SuspendLayout();
            try
            {
                // Reserve scrollbar width to keep the column breakpoint stable.
                int padding=P(6),gap=P(4),rowHeight=P(88);
                int width=Math.Max(1,modList.Width-SystemInformation.VerticalScrollBarWidth-2*padding-P(2));
                int columns=width>=P(1080) ? 2 : 1;
                int rowWidth=Math.Max(1,(width-columns*gap)/columns);
                modList.Padding=new Padding(padding);
                foreach(ModRow row in modRows)
                {
                    row.Margin=new Padding(0,0,gap,gap);row.Size=new Size(rowWidth,rowHeight);
                }
            }
            finally
            {
                modList.ResumeLayout(true);
                scroll.Y=Math.Min(scroll.Y,Math.Max(0,modList.DisplayRectangle.Height-modList.ClientSize.Height));
                modList.AutoScrollPosition=scroll;layingOutRows=false;
            }
        }
        protected override void OnResize(EventArgs e)
        {
            base.OnResize(e);LayoutControls();Invalidate();
        }
        private void ToggleFullscreen()
        {
            if(!isFullscreen)
            {
                Rectangle screen=Screen.FromControl(this).Bounds;
                windowedState=WindowState;windowedBounds=WindowState==FormWindowState.Normal ? Bounds : RestoreBounds;
                windowedMinimumSize=MinimumSize;isFullscreen=true;
                WindowState=FormWindowState.Normal;MinimumSize=Size.Empty;FormBorderStyle=FormBorderStyle.None;Bounds=screen;
            }
            else
            {
                isFullscreen=false;WindowState=FormWindowState.Normal;FormBorderStyle=FormBorderStyle.Sizable;
                MinimumSize=windowedMinimumSize;Bounds=windowedBounds;
                if(windowedState==FormWindowState.Maximized)WindowState=FormWindowState.Maximized;
            }
            fullscreen.Text=isFullscreen ? "退出全屏 Esc" : "全屏 F11";
            fullscreen.AccessibleName=isFullscreen ? "退出全屏" : "切换全屏";
            LayoutControls();Invalidate();
        }
        protected override bool ProcessCmdKey(ref Message msg,Keys keyData)
        {
            if(keyData==Keys.F11 || (keyData==Keys.Escape && isFullscreen)){ToggleFullscreen();return true;}
            return base.ProcessCmdKey(ref msg,keyData);
        }
        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g=e.Graphics;g.SmoothingMode=SmoothingMode.None;
            int width=(int)(ClientSize.Width/Skin.Scale),height=(int)(ClientSize.Height/Skin.Scale);
            g.DrawIcon(Icon,R(20,15,40,40));
            Skin.TextAt(g,"模组管理器",24,Skin.Gold,R(72,16,width-230,36),TextFormatFlags.SingleLine|TextFormatFlags.VerticalCenter);
            string path=state!=null ? state.gameDirectory : (target=="Experiment" ? Path.Combine(root,"playtest") : @"C:\Games\CubeWorld");
            Skin.TextAt(g,path,12,Skin.Muted,R(276,74,width-296,24),TextFormatFlags.SingleLine|TextFormatFlags.VerticalCenter|TextFormatFlags.EndEllipsis);
            using(Brush surface=new SolidBrush(Skin.Inset))g.FillRectangle(surface,R(20,114,width-40,34));
            using(Brush b=new SolidBrush(noticeColor))g.FillRectangle(b,R(30,128,6,6));
            Skin.TextAt(g,notice,13,noticeColor,R(46,118,width-80,26),TextFormatFlags.SingleLine|TextFormatFlags.VerticalCenter|TextFormatFlags.EndEllipsis);
            Skin.TextAt(g,feedback,12,feedbackColor,R(20,height-82,width-40,26),TextFormatFlags.WordBreak|TextFormatFlags.VerticalCenter|TextFormatFlags.EndEllipsis);
        }

        private async Task SelectTarget(string value)
        {
            if(busy || target==value)return;
            target=value;state=null;experiment.Selected=target=="Experiment";formal.Selected=target=="Formal";
            experiment.Invalidate();formal.Invalidate();feedback="更改会自动备份，下次启动生效。";feedbackColor=Skin.Muted;
            await RefreshState(false);
        }
        private void ApplyState()
        {
            ModState nav=null,pot=null,exp=null,light=null,bag=null,travel=null,batch=null,draw=null;
            if(state!=null && state.mods!=null) foreach(ModState mod in state.mods){if(mod.id=="Navigation")nav=mod;if(mod.id=="PotionShop")pot=mod;if(mod.id=="RegionExplorer")exp=mod;if(mod.id=="MapLight")light=mod;if(mod.id=="InventorySort")bag=mod;if(mod.id=="FastTravel")travel=mod;if(mod.id=="BatchActions")batch=mod;if(mod.id=="BossDraw")draw=mod;}
            bool canWrite=state!=null && state.canManage && writableShell;
            navigation.Apply(nav,canWrite,busy);potion.Apply(pot,canWrite,busy);explorer.Apply(exp,canWrite,busy);mapLight.Apply(light,canWrite,busy);inventorySort.Apply(bag,canWrite,busy);fastTravel.Apply(travel,canWrite,busy);batchActions.Apply(batch,canWrite,busy);bossDraw.Apply(draw,canWrite,busy);
            experiment.Enabled=!busy;formal.Enabled=!busy;refresh.Enabled=!busy;
            launch.Enabled=!busy && state!=null && state.canLaunch && writableShell;
            launch.Text=launching ? "启动中…" : (state!=null && state.gameRunning ? "游戏运行中" : "启动游戏");
            launch.AccessibleDescription=state==null ? "等待状态检查完成" : (state.canLaunch ? "启动当前选择的"+state.targetLabel : state.launchReason);
            Invalidate();
        }
        private async Task<bool> RefreshState(bool explicitRefresh)
        {
            if(busy)return false;
            busy=true;notice="正在读取模组状态…";noticeColor=Skin.Muted;ApplyState();
            try
            {
                string output=await RunScript(Path.Combine(root,"mod_manager","Get-ModManagerState.ps1"),"-Target "+target);
                ManagerState latest=new JavaScriptSerializer().Deserialize<ManagerState>(output.Trim());
                if(latest==null || latest.mods==null)throw new Exception("没有读到有效状态，请点击刷新重试。");
                state=latest;
                if(!writableShell){notice="缺少 PowerShell 7 运行环境，当前仅能查看状态。";noticeColor=Skin.Amber;}
                else if(!state.canManage)
                {
                    notice=state.gameRunning ? "游戏正在运行，正常退出后才能切换。" : (!state.processCheckOk ? "暂时无法确认游戏是否已退出，开关已锁定。" : (String.IsNullOrEmpty(state.blockReason) ? "暂时不能切换，请检查游戏目录。" : state.blockReason));
                    noticeColor=Skin.Amber;
                    if(!state.processCheckOk){feedback="请关闭游戏后刷新状态；仍无法读取时，请重新打开管理器。";feedbackColor=Skin.Muted;}
                }
                else {notice=target=="Formal" ? "正式副本已就绪。切换只影响当前选择的功能。" : "游戏已退出，可以自由切换。";noticeColor=Skin.Green;}
                if(explicitRefresh && state.canManage){feedback="状态已刷新。开关显示当前配置，更改在下次启动时生效。";feedbackColor=Skin.Muted;}
                if(state.canManage && !state.canLaunch){feedback="暂时不能启动："+state.launchReason;feedbackColor=Skin.Amber;}
                return true;
            }
            catch(Exception ex){state=null;notice="暂时无法读取状态，开关已锁定。";noticeColor=Skin.Amber;feedback=ex.Message;feedbackColor=Skin.Amber;return false;}
            finally{busy=false;ApplyState();}
        }
        private async Task ChangeMod(ModRow row)
        {
            if(busy || state==null || !state.canManage || !writableShell || row.State==null || !row.State.canToggle || !row.State.enabled.HasValue)return;
            bool enable=!row.State.enabled.Value;
            writing=true;busy=true;notice="正在保存，请稍等…";noticeColor=Skin.Gold;ApplyState();
            bool succeeded=false;string error=null;
            try{await RunScript(Path.Combine(root,"Manage-Mod.ps1"),"-Mod "+row.Id+" -Action "+(enable ? "Enable" : "Disable")+" -Target "+target);succeeded=true;}
            catch(Exception ex){error=ex.Message;}
            finally{busy=false;writing=false;}
            bool refreshed=await RefreshState(false);
            if(succeeded && refreshed)
            {
                ModState actual=null;foreach(ModState m in state.mods)if(m.id==row.Id)actual=m;
                if(actual!=null && actual.enabled.HasValue && actual.enabled.Value==enable){feedback=actual.name+(enable ? "已打开。" : "已关闭。")+"更改已保存，下次启动游戏时生效。";feedbackColor=Skin.Green;}
                else{feedback="操作已执行，但状态与预期不同，请刷新核对。";feedbackColor=Skin.Amber;}
            }
            else if(succeeded){feedback="更改已执行，状态尚未确认。请点击刷新核对。";feedbackColor=Skin.Amber;}
            else{feedback=String.IsNullOrEmpty(error) ? "没有完成切换，请刷新后重试。" : error;feedbackColor=Skin.Amber;}
            Invalidate();
        }
        private async Task LaunchGame()
        {
            if(busy || state==null || !state.canLaunch || !writableShell)return;
            launching=true;writing=true;busy=true;notice="正在检查并启动所选副本…";noticeColor=Skin.Gold;ApplyState();
            string error=null;bool requested=false;
            try
            {
                string output=await RunScript(Path.Combine(root,"mod_manager","Start-ModGame.ps1"),"-Target "+target);
                LaunchResult result=new JavaScriptSerializer().Deserialize<LaunchResult>(output.Trim());
                if(result==null || !result.requested || result.target!=target || result.processId<=0)throw new Exception("尚未确认启动请求，请刷新状态核对。");
                requested=true;
            }
            catch(Exception ex){error=ex.Message;}
            finally{busy=false;writing=false;launching=false;}
            await RefreshState(false);
            feedback=requested ? "已交给"+(target=="Formal" ? "正式副本" : "实验副本")+"启动器，请等待游戏窗口。" : error;
            feedbackColor=requested ? Skin.Green : Skin.Amber;Invalidate();
        }
        private async Task<string> RunScript(string script,string arguments)
        {
            if(!File.Exists(script))throw new Exception("缺少管理文件，请从完整项目目录打开管理器。");
            string command="$ErrorActionPreference='Stop'; [Console]::OutputEncoding=[Text.UTF8Encoding]::new($false); try { & '"+script.Replace("'","''")+"' "+arguments+"; exit 0 } catch { [Console]::Error.WriteLine($_.Exception.Message); exit 1 }";
            ProcessStartInfo info=new ProcessStartInfo(shell,"-NoLogo -NoProfile -NonInteractive -EncodedCommand "+Convert.ToBase64String(Encoding.Unicode.GetBytes(command)));
            info.WorkingDirectory=root;info.UseShellExecute=false;info.CreateNoWindow=true;info.WindowStyle=ProcessWindowStyle.Hidden;
            info.RedirectStandardOutput=true;info.RedirectStandardError=true;info.StandardOutputEncoding=Encoding.UTF8;info.StandardErrorEncoding=Encoding.UTF8;
            using(Process process=Process.Start(info))
            {
                Task<string> stdout=process.StandardOutput.ReadToEndAsync();Task<string> stderr=process.StandardError.ReadToEndAsync();
                await Task.Run(delegate{process.WaitForExit();});
                string output=await stdout;string errors=await stderr;
                if(process.ExitCode!=0)throw new Exception(String.IsNullOrWhiteSpace(errors) ? "没有完成操作，请刷新后重试。" : errors.Trim());
                return output;
            }
        }
        private void OpenBackups()
        {
            string path=Path.Combine(root,"backups");
            if(!Directory.Exists(path)){feedback="还没有备份。第一次更改开关时会自动保存。";feedbackColor=Skin.Muted;Invalidate();return;}
            try{Process.Start(new ProcessStartInfo("explorer.exe","\""+path+"\""){UseShellExecute=true});}
            catch(Exception ex){feedback=ex.Message;feedbackColor=Skin.Amber;Invalidate();}
        }
        private static string FindShell(out bool writable)
        {
            string[] candidates=new string[]{Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles),"PowerShell","7","pwsh.exe"),Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile),".cache","codex-runtimes","codex-primary-runtime","dependencies","native","powershell","pwsh.exe")};
            foreach(string candidate in candidates)if(File.Exists(candidate)){writable=true;return candidate;}
            foreach(string directory in (Environment.GetEnvironmentVariable("PATH") ?? "").Split(Path.PathSeparator))
            {
                try{string candidate=Path.Combine(directory.Trim().Trim('"'),"pwsh.exe");if(File.Exists(candidate)){writable=true;return candidate;}}catch(ArgumentException){}
            }
            writable=false;return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System),"WindowsPowerShell","v1.0","powershell.exe");
        }
    }
}
