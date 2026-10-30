#include "window.h"
#include <cstdio>
#include <shellapi.h>
#include <commdlg.h>
#include <shlobj.h>
#include <windowsx.h>
#include <mfmediaengine.h>
#include <winrt/base.h>
#include <sstream>
#include <iomanip>
#include <cmath>

namespace cvt {
enum Id {
 Add=100, Remove, Clear, Edit, Reset, Open, SettingsButton, Run, Cancel, Retry, Output, Browse,
 Operation=200, Mode, Target, Quality, OutputKind, VideoFormat, ImageFormat, AudioFormat, Encoder, Width, Height, Fps, VideoBitrate, AllowSdr, AllowAlpha, Background,
 Start=240, End, Speed, CropX, CropY, CropW, CropH, Rotation, HFlip, VFlip, Gray, Deinterlace, Denoise, Sharpen, Brightness, Contrast, Saturation, FastTrim, Snapshot, Video, Timeline, Play, MarkIn, MarkOut, PlayRange, EffectPreview, Clock,
 AudioTracks=290, VideoTrack, AudioBitrate, SampleRate, Channels, Gain, Mute, OnlyAudio,
 SubtitleTracks=310, SubtitleMode, SubtitleExternal, SubtitleBrowse, SubtitleUseExternal, SubtitleOffset, SubtitleFormat, BurnTrack, AllowStyleLoss, Extract,
 ConcatNormalize=340, ConcatConfirm, ConcatUp, ConcatDown, ConcatStart, ClipStart, ClipEnd, ClipSave, ConcatTarget,
 Language=360, ModernMenu, Presets, Register, Unregister, Hardware, SaveSettings, Summary, AllProgress, Log, PreviewStatus
};
constexpr UINT UI_EVENT = WM_APP + 2;
namespace {
// Layout is expressed in logical pixels at 96 DPI and scaled with px().
constexpr int kMargin = 10;
constexpr int kToolbarTop = 10;
constexpr int kToolbarHeight = 28;
constexpr int kListTop = 46;
constexpr int kListHeight = 140;
constexpr int kTabsTop = 196;
constexpr int kPagePadding = 12;
constexpr int kRowPitch = 28;
constexpr int kControlHeight = 22;
constexpr int kLabelColumn = 122;
constexpr int kLabelGap = 8;
constexpr int kColumnGap = 24;
constexpr int kButtonGap = 8;
constexpr int kBottomBar = 52;
constexpr int kOutputHeight = 30;
}
static void post(HWND window,std::unique_ptr<UiEvent> event) { if (PostMessageW(window,UI_EVENT,0,reinterpret_cast<LPARAM>(event.get()))) event.release(); }
static std::wstring size_text(uint64_t bytes) { std::wostringstream out; out << std::fixed << std::setprecision(2) << bytes/1000000. << L" MB"; return out.str(); }
static std::wstring decimal(double n) { std::wostringstream out; out.imbue(std::locale::classic()); out << std::setprecision(9) << n; return out.str(); }
static double parse(const std::wstring& value) { std::wistringstream stream(value); stream.imbue(std::locale::classic()); double n; stream >> n; if (!stream || !std::isfinite(n)) throw Error("Invalid numeric setting."); stream >> std::ws; if (!stream.eof()) throw Error("Invalid numeric setting."); return n; }
Window::Window(HINSTANCE instance,Settings settings) : instance_(instance),settings_(std::move(settings)),engine_(Engine::discover()) {}
Window::~Window() { shutdown(); if (font_) DeleteObject(font_); }
HWND Window::control(int id) const { auto it = controls_.find(id); return it == controls_.end() ? nullptr : it->second; }
int Window::px(int n) const { return MulDiv(n,dpi_,96); }
int Window::selected() const { return ListView_GetNextItem(list_,-1,LVNI_SELECTED); }
std::wstring Window::text(int id) const { HWND h = control(id); int n = GetWindowTextLengthW(h); std::wstring value(n+1,0); GetWindowTextW(h,value.data(),n+1); value.resize(n); return value; }
void Window::text(int id,const std::wstring& value) { SetWindowTextW(control(id),value.c_str()); }
bool Window::checked(int id) const { return SendMessageW(control(id),BM_GETCHECK,0,0) == BST_CHECKED; }
void Window::check(int id,bool value) { SendMessageW(control(id),BM_SETCHECK,value ? BST_CHECKED : BST_UNCHECKED,0); }
int Window::choice(int id) const { return static_cast<int>(SendMessageW(control(id),CB_GETCURSEL,0,0)); }
void Window::choice(int id,int value) { SendMessageW(control(id),CB_SETCURSEL,value,0); }
std::wstring Window::tr(const wchar_t* value) const {
 if (!settings_.chinese) return value;
 static const std::map<std::wstring,std::wstring> dictionary = {
 {L"Add files",L"添加文件"},{L"Remove",L"移除"},{L"Clear finished",L"清除完成项"},{L"Per-file settings",L"单独设置 / 确认"},{L"Save override",L"保存单独设置"},{L"Use batch settings",L"恢复批量设置"},{L"Open output",L"打开输出"},{L"Settings",L"设置"},
 {L"Start",L"开始处理"},{L"Cancel",L"取消整批"},{L"Retry failed",L"重试失败项"},{L"Browse",L"浏览…"},{L"Output folder (blank = beside each source)",L"输出目录（留空：各源文件旁 Convertii 文件夹）"},
 {L"Compression / conversion",L"压缩 / 转换"},{L"Trim / picture",L"剪辑 / 画面"},{L"Audio",L"音频"},{L"Subtitles",L"字幕"},{L"Concatenation",L"拼接"},{L"Tasks / settings",L"任务 / 设置"},
 {L"Operation",L"操作"},{L"Compress",L"压缩大小"},{L"Convert",L"格式转换"},{L"Stream copy / remux",L"无重编码换封装"},{L"Size mode",L"大小模式"},{L"Target size",L"目标上限"},{L"Quality preset",L"质量预设"},{L"Target / file (MB)",L"每个文件上限 (MB)"},{L"Clear",L"清晰"},{L"Balanced",L"均衡"},{L"Small",L"小体积"},
 {L"Output type",L"输出类型"},{L"Video",L"视频"},{L"Image",L"图片"},{L"Video format",L"视频格式"},{L"Image format",L"图片格式"},{L"Audio format",L"音频格式"},{L"Encoder",L"编码器"},{L"Auto",L"自动检测"},{L"Software",L"软件编码"},{L"Max width (0 = source)",L"最大宽度（0 = 原始）"},{L"Max height (0 = source)",L"最大高度（0 = 原始）"},{L"FPS (0 = source)",L"帧率（0 = 原始）"},{L"Video kbps (0 = auto)",L"视频 kbps（0 = 自动）"},
 {L"Allow HDR to SDR",L"确认 HDR 转为 SDR"},{L"Allow JPEG alpha fill",L"确认 JPEG 背景填充"},{L"Start (hh:mm:ss.ms)",L"开始 (时:分:秒.毫秒)"},{L"End (blank = full)",L"结束（空白 = 完整）"},{L"Speed (0.25 - 4)",L"速度（0.25～4）"},{L"Crop X",L"裁剪 X"},{L"Crop Y",L"裁剪 Y"},{L"Crop width (0 = off)",L"裁剪宽度（0 = 关闭）"},{L"Crop height",L"裁剪高度"},{L"Rotation",L"旋转"},{L"Horizontal flip",L"水平翻转"},{L"Vertical flip",L"垂直翻转"},{L"Grayscale",L"灰度"},{L"Deinterlace",L"去隔行"},{L"Denoise",L"降噪"},{L"Sharpen",L"锐化"},{L"Brightness (-1 - 1)",L"亮度（-1～1）"},{L"Contrast (0 - 3)",L"对比度（0～3）"},{L"Saturation (0 - 3)",L"饱和度（0～3）"},
 {L"Fast keyframe trim (time may differ)",L"快速关键帧截取（时间可能偏移）"},{L"Snapshot",L"当前位置截图"},{L"Play / pause",L"播放 / 暂停"},{L"Mark start",L"标记起点"},{L"Mark end",L"标记终点"},{L"Play selection",L"播放选区"},{L"Effect preview (10 s)",L"效果预览（10 秒）"},{L"Audio kbps",L"音频 kbps"},{L"Sample rate",L"采样率"},{L"Channels",L"声道"},{L"Gain dB (-60 - 60)",L"增益 dB（-60～60）"},{L"Mute",L"移除音频"},{L"Export audio only",L"仅导出音频"},{L"Video track",L"主视频轨"},
 {L"Subtitle action",L"字幕操作"},{L"None",L"无"},{L"Mux selected",L"封装选中字幕"},{L"Burn one",L"烧录一条字幕"},{L"Extract selected",L"提取选中字幕"},{L"External SRT / ASS",L"外部 SRT / ASS"},{L"Use external file",L"使用外部字幕"},{L"Offset seconds (+ = later)",L"偏移秒数（正数 = 延后）"},{L"Extract format",L"提取格式"},{L"Burn subtitle track",L"烧录字幕轨"},{L"Allow subtitle style loss",L"确认字幕转换可能丢失样式"},{L"Extract subtitles",L"提取字幕"},
 {L"Normalize / re-encode",L"统一参数后转码拼接"},{L"Confirm: one video and at most one audio, subtitles dropped",L"确认：只合并一路视频及至多一路音频，省略字幕"},{L"Move up",L"上移"},{L"Move down",L"下移"},{L"Join files",L"开始拼接"},{L"Clip start",L"片段开始"},{L"Clip end (blank = full)",L"片段结束（空白 = 完整）"},{L"Save clip range",L"保存片段范围"},
 {L"Language",L"语言"},{L"Windows 11 modern menu",L"启用 Windows 11 新右键菜单"},{L"Size presets (comma-separated MB)",L"大小预设（逗号分隔，MB）"},{L"Register / repair menus",L"注册 / 修复右键"},{L"Remove menus",L"移除右键"},{L"Test hardware",L"检测硬件"},{L"Save settings",L"保存设置"},{L"File",L"文件"},{L"Size",L"大小"},{L"Progress",L"进度"},{L"Status",L"状态"},{L"Output",L"输出"},{L"Queued",L"等待处理"},{L"Probing",L"正在分析"},{L"Running",L"处理中"},{L"Confirmation",L"需要确认"},{L"Completed",L"已完成"},{L"Failed",L"失败"},{L"Cancelled",L"已取消"},{L"Ready",L"就绪"},{L"Original playback",L"原片播放"},{L"Proxy playback",L"代理片段播放"},{L"Effect preview",L"效果样片预览"},
 {L"Source time",L"源时间"},{L"Details",L"详情"},
  {L"Convert format",L"格式转换"},{L"Picture",L"画面"},{L"Concat",L"拼接"},{L"Convert",L"转换"},{L"Save override",L"保存单独设置"},{L"File limit",L"单文件上限"},{L"Start time",L"开始时间"},{L"End time",L"结束时间"},{L"Speed",L"速度"},{L"Max width",L"最大宽度"},{L"Max height",L"最大高度"},{L"FPS",L"帧率"},{L"Video kbps",L"视频码率"},{L"Crop width",L"裁剪宽度"},{L"Brightness",L"亮度"},{L"Contrast",L"对比度"},{L"Saturation",L"饱和度"},{L"Gain dB",L"增益"},{L"Offset seconds",L"偏移秒数"},{L"Fast keyframe trim",L"快速关键帧截取"},{L"Play",L"播放"},{L"Effect preview",L"效果样片预览"},{L"JPEG RGB background",L"背景色"},{L"Size presets",L"大小预设"},{L"Clip end",L"片段结束"},{L"Apply target size to normalized output",L"对统一转码结果应用目标大小上限"},
  {L"0 = source",L"0 = 原始"},{L"0 = auto",L"0 = 自动"},{L"blank = full",L"留空 = 完整"},{L"0.25 - 4",L"0.25～4"},{L"-60 - 60",L"-60～60"},{L"-1 - 1",L"-1～1"},{L"0 - 3",L"0～3"},{L"0 = off",L"0 = 关闭"},{L"+ = later",L"正数 = 延后"},{L"Beside each source",L"留空则放在源文件旁"},{L"Subtitle file",L"字幕文件"},{L"hh:mm:ss.ms",L"时:分:秒.毫秒"},{L"Comma separated",L"逗号分隔"}
 }; auto it = dictionary.find(value); return it == dictionary.end() ? value : it->second;
}
HWND Window::create(const wchar_t* type,const std::wstring& title,DWORD style,HWND parent,int id,int x,int y,int w,int h,DWORD ex) {
 auto window = CreateWindowExW(ex,type,title.c_str(),WS_CHILD | WS_VISIBLE | style,px(x),px(y),px(w),px(h),parent,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),instance_,nullptr);
 if (!window) throw Error(win_error()); SendMessageW(window,WM_SETFONT,reinterpret_cast<WPARAM>(font_),TRUE); if (id) controls_[id] = window;
 if (style & WS_TABSTOP) SetWindowSubclass(window,focus_procedure,2,reinterpret_cast<DWORD_PTR>(this)); return window;
}
int Window::measure(const std::wstring& value,HFONT font) const {
 HDC dc = GetDC(window_); auto previous = SelectObject(dc,font ? font : font_); SIZE size{}; GetTextExtentPoint32W(dc,value.c_str(),static_cast<int>(value.size()),&size); SelectObject(dc,previous); ReleaseDC(window_,dc);
 return MulDiv(static_cast<int>(size.cx),96,dpi_);
}
HWND Window::make_button(HWND parent,int id,const wchar_t* label,bool checkbox,bool primary) {
 auto title = tr(label); auto style = WS_TABSTOP | (checkbox ? BS_AUTOCHECKBOX : primary ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON);
 auto window = create(L"BUTTON",title,style,parent,id,0,0,120,kControlHeight);
 widths_[id] = std::max(checkbox ? 0 : 80,measure(title,font_)+(checkbox ? 32 : 26)); labels_[id] = label; return window;
}
void Window::field(int page,int id,const wchar_t* label,int col,int row,int span,const wchar_t* type,const std::vector<std::wstring>& entries,const wchar_t* cue) {
 auto title = create(L"STATIC",tr(label),0,pages_[page],0,0,0,100,kControlHeight); page_controls_[page].push_back({title,col,row,1,0,0,true,true});
 bool presets = wcscmp(type,L"PRESETS") == 0; bool combo = presets || wcscmp(type,L"COMBOBOX") == 0; DWORD style = WS_TABSTOP;
 if (combo) style |= (presets ? CBS_DROPDOWN | CBS_AUTOHSCROLL : CBS_DROPDOWNLIST) | WS_VSCROLL; else style |= ES_AUTOHSCROLL | WS_BORDER;
 auto edit = create(presets ? L"COMBOBOX" : type,L"",style,pages_[page],id,0,0,160,combo ? 220 : kControlHeight);
 labels_[id] = label;
 for (auto& entry : entries) SendMessageW(edit,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(tr(entry.c_str()).c_str())); if (combo) SendMessageW(edit,CB_SETCURSEL,0,0);
 if (presets) { std::istringstream stream(settings_.presets); std::string value; while (std::getline(stream,value,',')) SendMessageW(edit,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(wide(value).c_str())); }
 page_controls_[page].push_back({edit,col,row,span,0,0,false,true});
 if (!combo && cue) { auto hint = tr(cue); SendMessageW(edit,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(hint.c_str())); }
}
void Window::button(int page,int id,const wchar_t* label,int col,int row,int span,bool checkbox,bool primary) {
 auto window = make_button(pages_[page],id,label,checkbox,primary); page_controls_[page].push_back({window,col,row,span,widths_[id],0,false,true,0,0,0,0});
}
HWND Window::open(int show) {
 WNDCLASSEXW page_class{sizeof(page_class)}; page_class.lpfnWndProc = DefWindowProcW; page_class.hInstance = instance_; page_class.hCursor = LoadCursorW(nullptr,IDC_ARROW); page_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE+1); page_class.lpszClassName = L"ConvertiiPage"; RegisterClassExW(&page_class);
 WNDCLASSEXW cls{sizeof(cls)}; cls.lpfnWndProc = procedure; cls.hInstance = instance_; cls.hCursor = LoadCursorW(nullptr,IDC_ARROW); cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE+1); cls.lpszClassName = L"ConvertiiNativeWindow"; cls.hIcon = LoadIconW(nullptr,IDI_APPLICATION); RegisterClassExW(&cls);
 dpi_ = static_cast<int>(GetDpiForSystem()); RECT work; SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0); int width = std::min(px(930),static_cast<int>(work.right-work.left)), height = std::min(px(780),static_cast<int>(work.bottom-work.top));
 auto window = CreateWindowExW(WS_EX_CONTROLPARENT,cls.lpszClassName,L"Convertii",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,width,height,nullptr,nullptr,instance_,this); if (!window) throw Error(win_error());
 ShowWindow(window,show); UpdateWindow(window); return window;
}
void Window::build() {
 NONCLIENTMETRICSW metrics{sizeof(metrics)}; SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS,sizeof(metrics),&metrics,0,dpi_);
 font_ = CreateFontIndirectW(&metrics.lfMessageFont);
 HDC device = GetDC(window_); TEXTMETRICW metrics_text{}; auto previous = SelectObject(device,font_); GetTextMetricsW(device,&metrics_text); SelectObject(device,previous); ReleaseDC(window_,device);
 label_height_ = MulDiv(static_cast<int>(metrics_text.tmHeight),96,dpi_); label_offset_ = (kControlHeight-label_height_)/2;
 make_button(window_,Add,L"Add files"); make_button(window_,Remove,L"Remove"); make_button(window_,Clear,L"Clear finished"); make_button(window_,Edit,L"Per-file settings");
 make_button(window_,Reset,L"Use batch settings"); make_button(window_,Open,L"Open output"); make_button(window_,SettingsButton,L"Settings");
 list_ = create(WC_LISTVIEWW,L"",WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,window_,0,0,0,200,kListHeight,WS_EX_CLIENTEDGE);
 ListView_SetExtendedListViewStyle(list_,LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
 const wchar_t* columns[] = {L"File",L"Size",L"Progress",L"Status",L"Output"};
 for (int i=0;i<5;++i) { auto title = tr(columns[i]); LVCOLUMNW column{}; column.mask = LVCF_TEXT | LVCF_WIDTH; column.pszText = title.data(); column.cx = px(120); ListView_InsertColumn(list_,i,&column); }
 tabs_ = create(WC_TABCONTROLW,L"",WS_TABSTOP | WS_CLIPSIBLINGS,window_,0,0,0,200,200);
 const wchar_t* tab_names[] = {L"Convert",L"Picture",L"Audio",L"Subtitles",L"Concat",L"Settings"};
 for (int i=0;i<6;++i) {
  auto title = tr(tab_names[i]); TCITEMW item{}; item.mask = TCIF_TEXT; item.pszText = title.data(); TabCtrl_InsertItem(tabs_,i,&item);
  pages_[i] = create(L"ConvertiiPage",L"",WS_CLIPCHILDREN | WS_CLIPSIBLINGS | WS_VSCROLL,window_,0,0,0,200,200,WS_EX_CONTROLPARENT); SetWindowSubclass(pages_[i],page_procedure,1,reinterpret_cast<DWORD_PTR>(this));
 }
 field(0,Operation,L"Operation",0,0,1,L"COMBOBOX",{L"Compress",L"Convert format",L"Stream copy / remux"}); field(0,OutputKind,L"Output type",1,0,1,L"COMBOBOX",{L"Video",L"Image",L"Audio"});
 field(0,Mode,L"Size mode",0,1,1,L"COMBOBOX",{L"Target size",L"Quality preset"}); field(0,Target,L"File limit",1,1,1,L"PRESETS");
 field(0,Quality,L"Quality preset",0,2,1,L"COMBOBOX",{L"Clear",L"Balanced",L"Small"}); field(0,Encoder,L"Encoder",1,2,1,L"COMBOBOX",{L"Auto",L"Software",L"NVIDIA",L"Intel",L"AMD"});
 field(0,VideoFormat,L"Video format",0,3,1,L"COMBOBOX",{L"mp4",L"mkv",L"webm"}); field(0,ImageFormat,L"Image format",1,3,1,L"COMBOBOX",{L"jpeg",L"png",L"webp"});
 field(0,Width,L"Max width",0,4,1,L"EDIT",{},L"0 = source"); field(0,Height,L"Max height",1,4,1,L"EDIT",{},L"0 = source");
 field(0,Fps,L"FPS",0,5,1,L"EDIT",{},L"0 = source"); field(0,VideoBitrate,L"Video kbps",1,5,1,L"EDIT",{},L"0 = auto");
 button(0,AllowSdr,L"Allow HDR to SDR",0,6,1,true); button(0,AllowAlpha,L"Allow JPEG alpha fill",1,6,1,true); field(0,Background,L"JPEG RGB background",0,7,1,L"EDIT",{},L"FFFFFF");
 auto preview = create(L"STATIC",L"",SS_BLACKRECT | WS_CLIPCHILDREN,pages_[1],Video,0,0,100,100); page_controls_[1].push_back({preview,0,0,1,0,190,false,true,0,0,0,0,true});
 field(1,Start,L"Start time",1,0,1,L"EDIT",{},L"hh:mm:ss.ms"); field(1,End,L"End time",1,1,1,L"EDIT",{},L"blank = full"); field(1,Speed,L"Speed",1,2,1,L"EDIT",{},L"0.25 - 4");
 button(1,FastTrim,L"Fast keyframe trim",1,3,1,true);
 button(1,Play,L"Play",-1,6,1); button(1,MarkIn,L"Mark start",-1,6,1); button(1,MarkOut,L"Mark end",-1,6,1); button(1,PlayRange,L"Play selection",-1,6,1); button(1,EffectPreview,L"Effect preview",-1,6,1); button(1,Snapshot,L"Snapshot",-1,6,1);
 for (auto& c : page_controls_[1]) if (c.row == 6 && c.col < 0) c.flush = true;
 auto slider = create(TRACKBAR_CLASSW,L"",WS_TABSTOP | TBS_HORZ | TBS_ENABLESELRANGE,pages_[1],Timeline,0,0,200,30); SendMessageW(slider,TBM_SETRANGEMAX,TRUE,100000); page_controls_[1].push_back({slider,-1,8,1,-146,0,false,true,0,0,0,0});
 auto clock = create(L"STATIC",L"00:00:00.000",0,pages_[1],Clock,0,0,130,24); page_controls_[1].push_back({clock,-1,8,1,130,0,false,true,0,0,0,0});
 auto preview_status = create(L"STATIC",tr(L"Original playback"),0,pages_[1],PreviewStatus,0,0,100,24); page_controls_[1].push_back({preview_status,-1,9,1,0,0,false,true,0,0,0,0});
 field(1,CropX,L"Crop X",0,10); field(1,CropY,L"Crop Y",1,10); field(1,CropW,L"Crop width",0,11,1,L"EDIT",{},L"0 = off"); field(1,CropH,L"Crop height",1,11);
 field(1,Rotation,L"Rotation",0,12,1,L"COMBOBOX",{L"0",L"90",L"180",L"270"}); button(1,HFlip,L"Horizontal flip",1,12,1,true); button(1,VFlip,L"Vertical flip",0,13,1,true); button(1,Gray,L"Grayscale",1,13,1,true);
 button(1,Deinterlace,L"Deinterlace",0,14,1,true); button(1,Denoise,L"Denoise",1,14,1,true); button(1,Sharpen,L"Sharpen",0,15,1,true); button(1,Brightness,L"Brightness",1,15,1,true);
 field(1,Contrast,L"Contrast",0,16,1,L"EDIT",{},L"0 - 3"); field(1,Saturation,L"Saturation",1,16,1,L"EDIT",{},L"0 - 3");
 field(2,AudioFormat,L"Audio format",0,0,1,L"COMBOBOX",{L"m4a",L"mp3",L"flac",L"wav"}); field(2,AudioBitrate,L"Audio kbps",1,0); field(2,SampleRate,L"Sample rate",0,1); field(2,Channels,L"Channels",1,1);
 field(2,Gain,L"Gain dB",0,2,1,L"EDIT",{},L"-60 - 60"); button(2,Mute,L"Mute",1,2,1,true); field(2,VideoTrack,L"Video track",0,3,1,L"COMBOBOX",{L"Auto"}); button(2,OnlyAudio,L"Export audio only",1,3,1,true);
 auto audio = create(WC_LISTVIEWW,L"",WS_TABSTOP | LVS_REPORT,pages_[2],AudioTracks,0,0,200,180,WS_EX_CLIENTEDGE); ListView_SetExtendedListViewStyle(audio,LVS_EX_FULLROWSELECT | LVS_EX_CHECKBOXES); page_controls_[2].push_back({audio,0,4,2,0,180,false,true,0,0,0,0});
 LVCOLUMNW track_column{}; track_column.mask = LVCF_WIDTH | LVCF_TEXT; track_column.cx = px(600); auto audio_title = tr(L"Audio"); track_column.pszText = audio_title.data(); ListView_InsertColumn(audio,0,&track_column);
 field(3,SubtitleMode,L"Subtitle action",0,0,1,L"COMBOBOX",{L"None",L"Mux selected",L"Burn one",L"Extract selected"}); field(3,SubtitleFormat,L"Extract format",1,0,1,L"COMBOBOX",{L"srt",L"ass"});
 field(3,SubtitleExternal,L"External SRT / ASS",0,1,1,L"EDIT",{},L"Subtitle file"); button(3,SubtitleUseExternal,L"Use external file",1,1,1,true);
 button(3,SubtitleBrowse,L"Browse",0,2,1); button(3,AllowStyleLoss,L"Allow subtitle style loss",1,2,1,true);
 field(3,SubtitleOffset,L"Offset seconds",0,3,1,L"EDIT",{},L"+ = later"); field(3,BurnTrack,L"Burn subtitle track",1,3,1,L"COMBOBOX",{L"Auto"});
 auto sublist = create(WC_LISTVIEWW,L"",WS_TABSTOP | LVS_REPORT,pages_[3],SubtitleTracks,0,0,200,170,WS_EX_CLIENTEDGE); ListView_SetExtendedListViewStyle(sublist,LVS_EX_FULLROWSELECT | LVS_EX_CHECKBOXES); page_controls_[3].push_back({sublist,0,4,2,0,170,false,true,0,0,0,0}); auto subtitle_title = tr(L"Subtitles"); track_column.pszText = subtitle_title.data(); ListView_InsertColumn(sublist,0,&track_column);
 button(3,Extract,L"Extract subtitles",-1,10,1);
 button(4,ConcatNormalize,L"Normalize / re-encode",0,0,2,true); button(4,ConcatConfirm,L"Confirm: one video and at most one audio, subtitles dropped",0,1,2,true);
 field(4,ClipStart,L"Clip start",0,2); field(4,ClipEnd,L"Clip end",1,2,1,L"EDIT",{},L"blank = full");
 button(4,ConcatTarget,L"Apply target size to normalized output",-1,3,1,true); button(4,ClipSave,L"Save clip range",-1,3,1);
 button(4,ConcatUp,L"Move up",-1,4,1); button(4,ConcatDown,L"Move down",-1,4,1); button(4,ConcatStart,L"Join files",-1,4,1,false,true);
 field(5,Language,L"Language",0,0,1,L"COMBOBOX",{L"English",L"简体中文"}); button(5,ModernMenu,L"Windows 11 modern menu",0,1,2,true);
 field(5,Presets,L"Size presets",0,2,1,L"EDIT",{},L"Comma separated");
 button(5,Register,L"Register / repair menus",-1,3,1); button(5,Unregister,L"Remove menus",-1,3,1); button(5,Hardware,L"Test hardware",-1,3,1); button(5,SaveSettings,L"Save settings",-1,3,1);
 auto log = create(L"EDIT",L"",WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_BORDER,pages_[5],Log,0,0,200,200,WS_EX_CLIENTEDGE); page_controls_[5].push_back({log,0,4,2,0,200,false,true,0,0,0,0});
 output_ = create(L"EDIT",L"",WS_TABSTOP | ES_AUTOHSCROLL,window_,Output,0,0,200,kOutputHeight);
 auto cue = tr(L"Beside each source"); SendMessageW(output_,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(cue.c_str()));
 make_button(window_,Browse,L"Browse"); status_ = create(L"STATIC",tr(L"Ready"),0,window_,0,0,0,200,24);
 auto progress = create(PROGRESS_CLASSW,L"",0,window_,AllProgress,0,0,10,4); SendMessageW(progress,PBM_SETRANGE32,0,1000); ShowWindow(progress,SW_HIDE);
 make_button(window_,Retry,L"Retry failed"); make_button(window_,Cancel,L"Cancel"); make_button(window_,Run,L"Start",false,true);
 DragAcceptFiles(window_,TRUE); load_options(settings_.options); choice(Language,settings_.chinese ? 1 : 0); check(ModernMenu,settings_.modern_menu); text(Presets,wide(settings_.presets)); text(ClipStart,L"0"); text(ClipEnd,L"");
 layout(); change_tab(); update_buttons(); SetTimer(window_,1,100,nullptr);
}

void Window::layout() {
 RECT client{0,0,0,0}; GetClientRect(window_,&client); if (size_.right>0) client = size_; int w = MulDiv(client.right,96,dpi_), h = MulDiv(client.bottom,96,dpi_);
 int x = kMargin; for (int id : {Add,Remove,Clear,Edit,Reset,Open}) { int width = widths_[id]; MoveWindow(control(id),px(x),px(kToolbarTop),px(width),px(kToolbarHeight),TRUE); x += width+kButtonGap; }
 int settings_width = widths_[SettingsButton]; MoveWindow(control(SettingsButton),px(w-kMargin-settings_width),px(kToolbarTop),px(settings_width),px(kToolbarHeight),TRUE);
 int list_width = std::max(160,w-2*kMargin);
 MoveWindow(list_,px(kMargin),px(kListTop),px(list_width),px(kListHeight),TRUE);
 RECT area; GetClientRect(list_,&area); int inner = MulDiv(area.right,96,dpi_);
 const int shares[] = {40,12,12,20,16}; for (int i=0;i<5;++i) ListView_SetColumnWidth(list_,i,px(inner*shares[i]/100));
 int bottom = h-kBottomBar, output_top = bottom-kButtonGap-kOutputHeight, progress_top = output_top-9, page_bottom = progress_top-6;

 int tabs_height = std::max(140,page_bottom-kTabsTop);
 MoveWindow(tabs_,px(kMargin),px(kTabsTop),px(list_width),px(tabs_height),TRUE);
 RECT display{0,0,px(list_width),px(tabs_height)}; TabCtrl_AdjustRect(tabs_,FALSE,&display);
 int page_width = std::max(px(160),static_cast<int>(display.right-display.left)), page_height = std::max(px(80),static_cast<int>(display.bottom-display.top));
 for (int i=0;i<6;++i) { MoveWindow(pages_[i],px(kMargin)+display.left,px(kTabsTop)+display.top,page_width,page_height,TRUE); layout_page(i); }
 int browse_width = widths_[Browse];
 MoveWindow(output_,px(kMargin),px(output_top),px(std::max(160,list_width-browse_width-kButtonGap)),px(kOutputHeight),TRUE);
 MoveWindow(control(Browse),px(w-kMargin-browse_width),px(output_top),px(browse_width),px(kOutputHeight),TRUE);
 MoveWindow(control(AllProgress),px(kMargin),px(progress_top),px(list_width),px(8),TRUE);
 int run_width = widths_[Run], cancel_width = widths_[Cancel], retry_width = widths_[Retry], button_y = bottom+(kBottomBar-kToolbarHeight)/2;
 int run_x = w-kMargin-run_width, cancel_x = run_x-kButtonGap-cancel_width, retry_x = cancel_x-kButtonGap-retry_width;
 MoveWindow(control(Run),px(run_x),px(button_y),px(run_width),px(kToolbarHeight),TRUE);
 MoveWindow(control(Cancel),px(cancel_x),px(button_y),px(cancel_width),px(kToolbarHeight),TRUE);
 MoveWindow(control(Retry),px(retry_x),px(button_y),px(retry_width),px(kToolbarHeight),TRUE);
 MoveWindow(status_,px(kMargin),px(bottom+10),px(std::max(80,retry_x-kMargin-kButtonGap)),px(kBottomBar-20),TRUE);
 player_.resize();
}

void Window::layout_page(int index) {
 RECT client; GetClientRect(pages_[index],&client);
 int width = MulDiv(client.right,96,dpi_)-MulDiv(GetSystemMetricsForDpi(SM_CXVSCROLL,dpi_),96,dpi_), height = MulDiv(client.bottom,96,dpi_);
 int available = std::max(240,width-2*kPagePadding), column = std::max(280,(available-kColumnGap)/2), field = std::max(120,column-kLabelColumn);

 std::map<int,int> flow_start, flow_cursor, flow_line;
 for (auto& c : page_controls_[index]) {
  if (!c.grid || c.col >= 0) continue;
  if (flow_start.find(c.row) == flow_start.end()) { flow_start[c.row] = c.flush ? kPagePadding : kPagePadding+kLabelColumn; flow_cursor[c.row] = flow_start[c.row]; flow_line[c.row] = 0; }
  int first = flow_start[c.row], usable = std::max(120,available-(first-kPagePadding));
  int w = c.width > 0 ? c.width : c.width < 0 ? std::max(120,usable+c.width) : usable;
  if (flow_cursor[c.row]+w > first+usable && flow_cursor[c.row] > first) { ++flow_line[c.row]; flow_cursor[c.row] = first; }
  c.x = flow_cursor[c.row]; c.w = w; c.h = std::max(c.height,kControlHeight); c.line = flow_line[c.row];
  flow_cursor[c.row] += w+kButtonGap;
 }

 std::map<int,int> rows; int last_row = -1;
 for (auto& c : page_controls_[index]) {
  if (!c.grid) continue;
  int span = c.line+std::max(1,(c.h+kRowPitch-1)/kRowPitch);
  if (c.row+span > last_row) last_row = c.row+span;
 }
 if (last_row < 0) { max_scroll_[index] = 0; scroll_[index] = 0; scroll_x_[index] = 0; return; }
 for (int r=0;r<=last_row;++r) rows[r] = kRowPitch;
 for (auto& c : page_controls_[index]) {
  if (!c.grid) continue;
  int span = c.line+std::max(1,(c.h+kRowPitch-1)/kRowPitch), need = c.line*kRowPitch+c.h, have = 0;
  for (int r=c.row;r<c.row+span && r<=last_row;++r) have += rows[r];
  if (need > have) { int add = (need-have+span-1)/span; for (int r=c.row;r<c.row+span && r<=last_row;++r) rows[r] += add; }
 }
 std::map<int,int> top; int y = kPagePadding;
 for (int r=0;r<=last_row;++r) { top[r] = y; y += rows[r]; }

 int content = 0;
 for (auto& c : page_controls_[index]) {
  if (!c.grid) { if (c.y+c.h > content) content = c.y+c.h; continue; }
  if (c.col >= 0 && c.label) {
   c.x = kPagePadding+c.col*(column+kColumnGap); c.y = top[c.row]+label_offset_; c.w = kLabelColumn-kLabelGap; c.h = label_height_;
  } else if (c.col >= 0) {
   c.x = c.flush ? kPagePadding+c.col*(column+kColumnGap) : c.col == 0 && c.span == 2 ? kPagePadding : kPagePadding+c.col*(column+kColumnGap)+kLabelColumn;
   c.y = top[c.row];
   c.w = c.width > 0 ? c.width : c.flush ? (c.span == 2 ? available : column) : c.col == 0 && c.span == 2 ? available : c.span == 2 ? std::max(120,available-kLabelColumn-kButtonGap-field) : field;
   c.h = c.height > 0 ? c.height : kControlHeight;
  } else {
   c.y = top[c.row]+c.line*kRowPitch;
  }
  if (c.y+c.h > content) content = c.y+c.h;
 }
 max_scroll_[index] = std::max(0,content+kPagePadding-std::max(1,height));
 scroll_[index] = std::clamp(scroll_[index],0,max_scroll_[index]); scroll_x_[index] = 0;
 for (auto& c : page_controls_[index]) MoveWindow(c.window,px(c.x),px(c.y-scroll_[index]),px(c.w),px(c.h),TRUE);
 SCROLLINFO info{sizeof(info)}; info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS; info.nMin = 0;
 info.nMax = max_scroll_[index]+std::max(1,height)-1; info.nPage = static_cast<UINT>(std::max(1,height)); info.nPos = scroll_[index];
 SetScrollInfo(pages_[index],SB_VERT,&info,TRUE);
}

void Window::change_tab() {
 int next = TabCtrl_GetCurSel(tabs_); if (next < 0) next = 0;
 if (page_ == 1 && next != 1) stop_preview(); page_ = next;
 for (int i=0;i<6;++i) ShowWindow(pages_[i],i==page_ ? SW_SHOW : SW_HIDE);
 SetWindowPos(pages_[page_],HWND_TOP,0,0,0,0,SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
 layout_page(page_); if (page_ == 1 && media_) text(Clock,wide(time_text(position_)));
}
void Window::load_options(const Options& o) {
 loading_ = true; choice(Operation,o.action == Action::Remux ? 2 : o.action == Action::Convert ? 1 : 0); choice(Mode,o.target_mode ? 0 : 1); text(Target,decimal(o.target_mb)); choice(Quality,o.quality); choice(OutputKind,static_cast<int>(o.output_kind));
 auto select_string = [&](int id,const std::string& value) { int count = static_cast<int>(SendMessageW(control(id),CB_GETCOUNT,0,0)); for (int i=0;i<count;++i) { wchar_t label[128]; SendMessageW(control(id),CB_GETLBTEXT,i,reinterpret_cast<LPARAM>(label)); if (utf8(label) == value) { choice(id,i); break; } } };
 select_string(VideoFormat,o.video_format); select_string(ImageFormat,o.image_format); select_string(AudioFormat,o.audio_format);
 choice(Encoder,o.encoder == "software" ? 1 : o.encoder == "nvidia" ? 2 : o.encoder == "intel" ? 3 : o.encoder == "amd" ? 4 : 0);
 text(Width,std::to_wstring(o.width)); text(Height,std::to_wstring(o.height)); text(Fps,decimal(o.fps)); text(VideoBitrate,std::to_wstring(o.video_kbps)); check(AllowSdr,o.allow_sdr); check(AllowAlpha,o.allow_alpha); text(Background,wide(o.background));
 text(Start,wide(time_text(o.start))); text(End,o.end > 0 ? wide(time_text(o.end)) : L""); text(Speed,decimal(o.speed)); text(CropX,std::to_wstring(o.crop_x)); text(CropY,std::to_wstring(o.crop_y)); text(CropW,std::to_wstring(o.crop_w)); text(CropH,std::to_wstring(o.crop_h)); choice(Rotation,o.rotation/90);
 check(HFlip,o.hflip); check(VFlip,o.vflip); check(Gray,o.gray); check(Deinterlace,o.deinterlace); check(Denoise,o.denoise); check(Sharpen,o.sharpen); text(Brightness,decimal(o.brightness)); text(Contrast,decimal(o.contrast)); text(Saturation,decimal(o.saturation)); check(FastTrim,o.fast_trim);
 text(AudioBitrate,std::to_wstring(o.audio_kbps)); text(SampleRate,std::to_wstring(o.sample_rate)); text(Channels,std::to_wstring(o.channels)); text(Gain,decimal(o.volume_db)); check(Mute,o.mute); check(OnlyAudio,o.output_kind == Kind::Audio); text(Output,o.output_directory.wstring());
 text(SubtitleExternal,o.external_subtitle.wstring()); check(SubtitleUseExternal,o.burn_external || o.mux_external); text(SubtitleOffset,decimal(o.subtitle_offset)); choice(SubtitleFormat,o.subtitle_format == "ass" ? 1 : 0); check(AllowStyleLoss,o.allow_style_loss); choice(SubtitleMode,o.burn_external || o.burn_subtitle >= 0 ? 2 : o.mux_external || !o.subtitle_tracks.empty() ? 1 : 0);
 check(ConcatNormalize,o.normalize_concat); check(ConcatConfirm,o.confirm_concat); audio_dirty_ = !o.audio_tracks.empty(); loading_ = false;
 check(ConcatTarget,o.action==Action::Concat && o.target_mode); if(o.action==Action::ExtractSubtitles) choice(SubtitleMode,3);
 loading_ = true;
 for (int id : {VideoTrack,BurnTrack}) for (int i=0;i<static_cast<int>(SendMessageW(control(id),CB_GETCOUNT,0,0));++i) {
  int track = static_cast<int>(SendMessageW(control(id),CB_GETITEMDATA,i,0)); if ((id==VideoTrack && track==o.video_track && i>0) || (id==BurnTrack && track==o.burn_subtitle && i>0)) choice(id,i);
 }
 for (int id : {AudioTracks,SubtitleTracks}) for (int i=0;i<ListView_GetItemCount(control(id));++i) {
  LVITEMW item{}; item.mask=LVIF_PARAM; item.iItem=i; ListView_GetItem(control(id),&item); auto& selected_tracks=id==AudioTracks ? o.audio_tracks : o.subtitle_tracks;
  bool selected=std::find(selected_tracks.begin(),selected_tracks.end(),static_cast<int>(item.lParam))!=selected_tracks.end();
  if (id==AudioTracks && selected_tracks.empty() && media_ && !o.mute) selected=static_cast<int>(item.lParam)==media_->audio;
  ListView_SetCheckState(control(id),i,selected);
 }
 loading_ = false;
}
Options Window::options() const {
 Options o; auto integer = [&](int id) { double value = parse(text(id)); if (value < INT_MIN || value > INT_MAX || std::floor(value) != value) throw Error("Expected an integer parameter."); return static_cast<int>(value); };
 o.action = choice(Operation) == 2 ? Action::Remux : choice(Operation) == 1 ? Action::Convert : Action::Compress; o.target_mode = choice(Mode) == 0; o.target_mb = parse(text(Target)); o.quality = choice(Quality); o.output_kind = static_cast<Kind>(choice(OutputKind));
 o.video_format = utf8(text(VideoFormat)); o.image_format = utf8(text(ImageFormat)); o.audio_format = utf8(text(AudioFormat)); const char* encoders[] = {"auto","software","nvidia","intel","amd"}; int enc = choice(Encoder); o.encoder = encoders[std::clamp(enc,0,4)];
 o.width = integer(Width); o.height = integer(Height); o.fps = parse(text(Fps)); o.video_kbps = integer(VideoBitrate); o.allow_sdr = checked(AllowSdr); o.allow_alpha = checked(AllowAlpha); o.background = utf8(text(Background));
 o.start = time_value(utf8(text(Start))); o.end = time_value(utf8(text(End))); o.speed = parse(text(Speed)); o.crop_x = integer(CropX); o.crop_y = integer(CropY); o.crop_w = integer(CropW); o.crop_h = integer(CropH); o.rotation = choice(Rotation)*90;
 o.hflip = checked(HFlip); o.vflip = checked(VFlip); o.gray = checked(Gray); o.deinterlace = checked(Deinterlace); o.denoise = checked(Denoise); o.sharpen = checked(Sharpen); o.brightness = parse(text(Brightness)); o.contrast = parse(text(Contrast)); o.saturation = parse(text(Saturation)); o.fast_trim = checked(FastTrim);
 o.audio_kbps = integer(AudioBitrate); o.sample_rate = integer(SampleRate); o.channels = integer(Channels); o.volume_db = parse(text(Gain)); o.mute = checked(Mute); if (checked(OnlyAudio)) o.output_kind = Kind::Audio;
 int video_choice = choice(VideoTrack); if (video_choice > 0) o.video_track = static_cast<int>(SendMessageW(control(VideoTrack),CB_GETITEMDATA,video_choice,0));
 if (audio_dirty_) for (int i=0;i<ListView_GetItemCount(control(AudioTracks));++i) if (ListView_GetCheckState(control(AudioTracks),i)) { LVITEMW item{}; item.mask = LVIF_PARAM; item.iItem = i; ListView_GetItem(control(AudioTracks),&item); o.audio_tracks.push_back(static_cast<int>(item.lParam)); }
 if (audio_dirty_ && o.audio_tracks.empty()) o.mute=true;
 o.external_subtitle = text(SubtitleExternal); o.subtitle_offset = parse(text(SubtitleOffset)); o.subtitle_format = choice(SubtitleFormat) == 1 ? "ass" : "srt"; o.allow_style_loss = checked(AllowStyleLoss);
 int mode = choice(SubtitleMode); if (mode == 1 || mode == 3) for (int i=0;i<ListView_GetItemCount(control(SubtitleTracks));++i) if (ListView_GetCheckState(control(SubtitleTracks),i)) { LVITEMW item{}; item.mask = LVIF_PARAM; item.iItem = i; ListView_GetItem(control(SubtitleTracks),&item); o.subtitle_tracks.push_back(static_cast<int>(item.lParam)); }
 if (mode == 2) { o.burn_external = checked(SubtitleUseExternal); if (!o.burn_external && choice(BurnTrack) > 0) o.burn_subtitle = static_cast<int>(SendMessageW(control(BurnTrack),CB_GETITEMDATA,choice(BurnTrack),0)); if(!o.burn_external && o.burn_subtitle<0) throw Error("Select a text subtitle track to burn."); }
 o.mux_external = checked(SubtitleUseExternal) && (mode == 1 || mode == 3); o.normalize_concat = checked(ConcatNormalize); o.confirm_concat = checked(ConcatConfirm); o.output_directory = text(Output); o.snapshot_time = position_; o.validate(); return o;
}
void Window::add_files(const Launch& launch) {
 if (launch.action && jobs_.empty() && !busy_) choice(Operation,*launch.action == Action::Convert ? 1 : 0);
 for (auto& path : launch.files) {
  if (!fs::is_regular_file(path)) continue; auto full = fs::absolute(path).lexically_normal(); if (std::any_of(jobs_.begin(),jobs_.end(),[&](auto& j) { return j.request.inputs.size()==1 && j.state!=State::Completed && j.state!=State::Failed && j.state!=State::Cancelled && _wcsicmp(j.request.inputs[0].path.c_str(),full.c_str()) == 0; })) continue;
  Job job; job.request.inputs.push_back({full}); job.request.options = settings_.options; if (launch.action && (busy_ || !jobs_.empty())) { try { job.request.options = options(); } catch (...) {} job.request.options.action = *launch.action; job.override_settings = true; } jobs_.push_back(std::move(job));
 }
 update_list(); if (selected() < 0 && !jobs_.empty()) ListView_SetItemState(list_,0,LVIS_SELECTED | LVIS_FOCUSED,LVIS_SELECTED | LVIS_FOCUSED); update_buttons();
}
void Window::select_file() {
 int index = selected(); if (index < 0 || static_cast<size_t>(index) >= jobs_.size()) return; stop_preview(); position_ = 0; media_.reset();
 auto& job = jobs_[index]; text(Log,wide(job.detail)); text(ClipStart,wide(time_text(job.request.inputs[0].start))); text(ClipEnd,job.request.inputs[0].end > 0 ? wide(time_text(job.request.inputs[0].end)) : L"");
 probe_cancel_ = true; if (probe_worker_.joinable()) probe_worker_.join(); probe_cancel_ = false; auto path = job.request.inputs[0].path; auto generation=++probe_generation_;
 probe_worker_ = std::thread([this,path,index,generation] { try { auto media = probe(engine_,path,&probe_cancel_); auto event = std::make_unique<UiEvent>(); event->origin=1; event->generation=generation; event->type = UiEvent::Type::Probe; event->index = index; event->media = std::move(media); post(window_,std::move(event)); } catch (const Cancelled&) {} catch (const std::exception& error) { auto event = std::make_unique<UiEvent>(); event->origin=1; event->generation=generation; event->type = UiEvent::Type::Error; event->index = index; event->message = error.what(); post(window_,std::move(event)); } }); update_buttons();
}
void Window::update_list() {
 suppress_selection_ = true; const wchar_t* states[] = {L"Queued",L"Probing",L"Running",L"Confirmation",L"Completed",L"Failed",L"Cancelled"};
 while (ListView_GetItemCount(list_) > static_cast<int>(jobs_.size())) ListView_DeleteItem(list_,ListView_GetItemCount(list_)-1);
 for (size_t i=0;i<jobs_.size();++i) {
  auto& job = jobs_[i]; auto name = job.request.inputs[0].path.filename().wstring() + (job.request.inputs.size() > 1 ? L" (+"+std::to_wstring(job.request.inputs.size()-1)+L")" : L"") + (job.override_settings ? L" *" : L"");
  if (i >= static_cast<size_t>(ListView_GetItemCount(list_))) { LVITEMW item{}; item.mask = LVIF_TEXT; item.iItem = static_cast<int>(i); item.pszText = name.data(); ListView_InsertItem(list_,&item); } else ListView_SetItemText(list_,static_cast<int>(i),0,name.data());
  std::error_code error; auto size = fs::file_size(job.request.inputs[0].path,error); auto input_size = size_text(error ? 0 : size); ListView_SetItemText(list_,static_cast<int>(i),1,input_size.data());
  auto percent = std::to_wstring(static_cast<int>(job.fraction*100))+L"%"; ListView_SetItemText(list_,static_cast<int>(i),2,percent.data()); auto state = tr(states[static_cast<int>(job.state)]); ListView_SetItemText(list_,static_cast<int>(i),3,state.data());
  uint64_t output_bytes = 0; for (auto& output : job.result.outputs) output_bytes += output.bytes; auto output = output_bytes ? size_text(output_bytes) : L"—"; ListView_SetItemText(list_,static_cast<int>(i),4,output.data());
 } suppress_selection_ = false;
}
void Window::update_buttons() {
 int index = selected(); bool has = index >= 0 && static_cast<size_t>(index) < jobs_.size();
 for (int id : {Remove,Clear,Edit,Reset,Run,Retry,ConcatStart,ConcatUp,ConcatDown,ClipSave,Extract,Snapshot,SaveSettings,Register,Unregister,Hardware}) EnableWindow(control(id),!busy_);
 EnableWindow(control(Cancel),busy_ || preview_busy_); for (int id : {Edit,Reset,Remove,ConcatUp,ConcatDown,ClipSave}) EnableWindow(control(id),!busy_ && has);
 EnableWindow(control(Run),!busy_ && !editing_ && std::any_of(jobs_.begin(),jobs_.end(),[](auto& j) { return j.state == State::Queued; }));
 EnableWindow(control(Retry),!busy_ && std::any_of(jobs_.begin(),jobs_.end(),[](auto& j) { return j.state == State::Failed || j.state == State::Cancelled; }));
 EnableWindow(control(Open),has && !jobs_[index].result.outputs.empty()); EnableWindow(control(EffectPreview),!busy_ && media_ && media_->kind == Kind::Video);
}
void Window::execute(bool retry,bool concat,bool snapshot,bool subtitle) {
 if (busy_ || editing_) return; auto global = options(); settings_.options = global; save_settings(settings_); stop_preview();
 std::vector<std::pair<size_t,Request>> requests;
 if (concat) { if (jobs_.size() < 2) throw Error("At least two files are required for concat."); Request request; request.options = global; request.options.action = Action::Concat; request.options.target_mode = checked(ConcatTarget);
  if (request.options.target_mode && !request.options.normalize_concat) throw Error("A target size requires normalized re-encoding.");
  for (auto& job : jobs_) { if (job.request.inputs.size() != 1) continue; request.inputs.push_back(job.request.inputs[0]); }
  Job job; job.request = request; job.override_settings = true; jobs_.push_back(std::move(job)); requests.push_back({jobs_.size()-1,request});
 } else if (snapshot || subtitle) {
  int index = selected(); if (index < 0) throw Error("Select a file first."); Request request = jobs_[index].request; request.options = global; request.options.action = snapshot ? Action::Snapshot : Action::ExtractSubtitles; request.options.snapshot_time = position_; Job job; job.request = request; job.override_settings = true; jobs_.push_back(std::move(job)); requests.push_back({jobs_.size()-1,request});
 } else {
  for (size_t i=0;i<jobs_.size();++i) { auto& job = jobs_[i]; bool eligible = retry ? job.state == State::Failed || job.state == State::Cancelled : job.state == State::Queued;
   if (eligible) { if (!job.override_settings) job.request.options = global; if (choice(SubtitleMode)==3) job.request.options.action = Action::ExtractSubtitles; requests.push_back({i,job.request}); }
  }
 }
 if (requests.empty()) return; if (batch_.joinable()) batch_.join(); cancel_ = false; busy_ = true; update_list(); update_buttons();
 batch_ = std::thread([this,requests=std::move(requests)] {
  for (auto& [index,request] : requests) {
   auto result = process(engine_,request,&cancel_,[this,index](const Progress& progress) { auto event = std::make_unique<UiEvent>(); event->type = UiEvent::Type::Progress; event->index = index; event->progress = progress; post(window_,std::move(event)); });
   auto event = std::make_unique<UiEvent>(); event->type = UiEvent::Type::Result; event->index = index; event->result = std::move(result); post(window_,std::move(event));
  }
  auto event = std::make_unique<UiEvent>(); event->type = UiEvent::Type::Done; post(window_,std::move(event));
 });
}
void Window::on_event(std::unique_ptr<UiEvent> event) {
 if ((event->origin==1 && event->generation!=probe_generation_) || (event->origin==2 && event->generation!=preview_generation_)) return;
 if(event->origin==2) { preview_busy_=false; update_buttons(); }
 if (event->type == UiEvent::Type::Progress && event->index < jobs_.size()) {
  auto& job = jobs_[event->index]; job.state = State::Running; job.fraction = event->progress.fraction; job.detail = event->progress.message;
  SetWindowTextW(status_,wide(job.detail).c_str()); SendMessageW(control(AllProgress),PBM_SETPOS,static_cast<WPARAM>(job.fraction*1000),0); ShowWindow(control(AllProgress),SW_SHOW); update_list();
 } else if (event->type == UiEvent::Type::Result && event->index < jobs_.size()) {
  auto& job = jobs_[event->index]; job.result = std::move(event->result); job.state = job.result.state; job.detail = job.result.message; if (job.state == State::Completed) job.fraction = 1;
  if (selected() == static_cast<int>(event->index)) { std::wstring message = wide(job.detail); for (auto& output : job.result.outputs) message += L"\r\n"+output.path.wstring(); text(Log,message); } update_list();
 } else if (event->type == UiEvent::Type::Done) {
  busy_ = false; if (batch_.joinable()) batch_.join(); SetWindowTextW(status_,tr(L"Ready").c_str()); update_buttons(); SendMessageW(control(AllProgress),PBM_SETPOS,0,0); ShowWindow(control(AllProgress),SW_HIDE); if (closing_) DestroyWindow(window_);
 } else if (event->type == UiEvent::Type::Probe && event->index == static_cast<size_t>(selected())) {
  media_ = std::move(event->media); loading_ = true; SendMessageW(control(VideoTrack),CB_RESETCONTENT,0,0); SendMessageW(control(VideoTrack),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(tr(L"Auto").c_str())); choice(VideoTrack,0);
  ListView_DeleteAllItems(control(AudioTracks)); ListView_DeleteAllItems(control(SubtitleTracks)); SendMessageW(control(BurnTrack),CB_RESETCONTENT,0,0); SendMessageW(control(BurnTrack),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(tr(L"None").c_str())); choice(BurnTrack,0);
  for (auto& track : media_->tracks) {
   auto label = L"#"+std::to_wstring(track.index)+L"  "+wide(track.codec)+L"  "+wide(track.language)+L"  "+wide(track.title)+(track.preferred ? L" [default]" : L"");
   if (track.type == "video" && !track.attached) { auto pos = SendMessageW(control(VideoTrack),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str())); SendMessageW(control(VideoTrack),CB_SETITEMDATA,pos,track.index); }
   if (track.type == "audio" || track.type == "subtitle") {
    HWND list = control(track.type == "audio" ? AudioTracks : SubtitleTracks); LVITEMW item{}; item.mask = LVIF_TEXT | LVIF_PARAM; item.iItem = ListView_GetItemCount(list); item.pszText = label.data(); item.lParam = track.index; auto row = ListView_InsertItem(list,&item);
    if (track.type == "audio") { ListView_SetCheckState(list,row,track.index == media_->audio); }
    else if (track.text_subtitle) { auto pos = SendMessageW(control(BurnTrack),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str())); SendMessageW(control(BurnTrack),CB_SETITEMDATA,pos,track.index); }
   }
  }
  audio_dirty_ = false; loading_ = false; update_buttons(); text(Clock,wide(time_text(position_))+L" / "+wide(time_text(media_->duration)));
 } else if (event->type == UiEvent::Type::Preview) {
  if (busy_ || page_ != 1 || preview_cancel_) return;
  proxy_ = true; effects_ = event->preview.effects; proxy_start_ = event->preview.source_start; proxy_speed_ = event->preview.speed; playing_file_ = event->preview.file;
  try { player_.open(control(Video),window_,playing_file_); text(PreviewStatus,tr(effects_ ? L"Effect preview" : L"Proxy playback")); } catch (const std::exception& e) { text(PreviewStatus,wide(e.what())); }
 } else if (event->type == UiEvent::Type::Info) { text(Log,wide(event->message)); update_buttons(); }
 else if (event->type == UiEvent::Type::Error) { text(Log,wide(event->message)); SetWindowTextW(status_,wide(event->message).c_str()); text(PreviewStatus,wide(event->message)); }
}
void Window::edit_selected() {
 int index = selected(); if (index < 0 || busy_) return;
 if (!editing_) {
  global_before_edit_ = options(); editing_ = true; editing_index_ = index; auto& job = jobs_[index]; load_options(job.override_settings ? job.request.options : global_before_edit_); EnableWindow(list_,FALSE); text(Edit,tr(L"Save override"));
  if (job.state == State::Confirmation) {
   int tab = job.result.confirmation == Confirmation::Concat ? 4 : job.result.confirmation == Confirmation::SubtitleStyles ? 3 : 0; TabCtrl_SetCurSel(tabs_,tab); change_tab();
   MessageBoxW(window_,wide(job.detail).c_str(),L"Convertii",MB_OK | MB_ICONINFORMATION);
  }
 } else {
  auto value = options(); auto& job = jobs_[editing_index_]; auto action=job.request.options.action;
  if(action==Action::Concat || action==Action::Snapshot || action==Action::ExtractSubtitles) value.action=action;
  if(action==Action::Concat) value.target_mode=checked(ConcatTarget); if(action==Action::Snapshot) value.snapshot_time=job.request.options.snapshot_time;
  job.request.options = value; job.override_settings = true; job.state = State::Queued; job.detail.clear(); job.fraction = 0; editing_ = false; load_options(global_before_edit_); EnableWindow(list_,TRUE); text(Edit,tr(L"Per-file settings"));
 }
 update_list(); update_buttons();
}
void Window::stop_preview() { ++preview_generation_; preview_cancel_ = true; if (preview_worker_.joinable()) preview_worker_.join(); player_.close(); proxy_ = effects_ = false; selection_play_ = false; playing_file_.clear(); pending_seek_ = false; preview_busy_=false; update_buttons(); }
void Window::preview(bool effects) {
 if (busy_ || !media_ || media_->kind != Kind::Video) return; int index = selected(); if (index < 0) return; auto o = options(); preview_cancel_ = true; if (preview_worker_.joinable()) preview_worker_.join(); preview_cancel_ = false; player_.close();
 text(PreviewStatus,settings_.chinese ? L"正在生成预览，可取消或拖动到其他位置…" : L"Rendering preview; seeking cancels the previous request…"); auto input = jobs_[index].request.inputs[0]; double position = position_; auto generation=++preview_generation_;
 preview_busy_=true; update_buttons();
 preview_worker_ = std::thread([this,input,o,position,effects,generation] { try { auto result = render_preview(engine_,input,o,position,effects,&preview_cancel_); auto event = std::make_unique<UiEvent>(); event->origin=2; event->generation=generation; event->type = UiEvent::Type::Preview; event->preview = std::move(result); post(window_,std::move(event)); } catch (const Cancelled&) {} catch (const std::exception& e) { auto event = std::make_unique<UiEvent>(); event->origin=2; event->generation=generation; event->type = UiEvent::Type::Error; event->message = e.what(); post(window_,std::move(event)); } });
}
void Window::play() {
 if (busy_ || !media_ || media_->kind != Kind::Video) return;
 if (!player_.exists()) {
  try { playing_file_ = media_->path; proxy_ = effects_ = false; player_.open(control(Video),window_,playing_file_); text(PreviewStatus,tr(L"Original playback")); }
  catch (const std::exception&) { preview(false); } return;
 }
 if (player_.paused()) player_.play(); else player_.pause();
}
void Window::seek(double seconds) {
 if (!media_) return; position_ = std::clamp(seconds,0.,std::max(0.,media_->duration-.01));
 if (player_.exists() && (!proxy_ || (position_ >= proxy_start_ && position_ < proxy_start_+player_.duration()*proxy_speed_))) player_.seek(proxy_ ? (position_-proxy_start_)/proxy_speed_ : position_);
 else if (!busy_ && page_ == 1) preview(false);
 text(Clock,wide(time_text(position_))+L" / "+wide(time_text(media_->duration)));
}
void Window::timer() {
 if (pending_seek_ && GetTickCount64() >= reinterpret_cast<ULONGLONG>(GetPropW(window_,L"SeekDue"))) { pending_seek_ = false; seek(seek_target_); }
 if (player_.is_ready() && !seeking_) {
  double seconds = player_.position(); if (std::isfinite(seconds)) position_ = proxy_ ? proxy_start_+seconds*proxy_speed_ : seconds;
  if (media_ && media_->duration > 0) SendMessageW(control(Timeline),TBM_SETPOS,TRUE,static_cast<LPARAM>(position_/media_->duration*100000)); text(Clock,wide(time_text(position_))+(media_ ? L" / "+wide(time_text(media_->duration)) : L""));
  if (selection_play_) { double end = time_value(utf8(text(End))); if (end > 0 && position_ >= end) { player_.pause(); selection_play_ = false; } }
 }
}
void Window::service(std::function<std::string()> action) {
 if (service_worker_.joinable()) service_worker_.join();
 service_worker_ = std::thread([this,action] { auto event = std::make_unique<UiEvent>(); event->type = UiEvent::Type::Info;
  try { winrt::init_apartment(winrt::apartment_type::multi_threaded); event->message = action(); } catch (const winrt::hresult_error& e) { event->message = utf8(e.message().c_str()); } catch (const std::exception& e) { event->message = e.what(); } post(window_,std::move(event)); });
}
void Window::command(int id) {
 if (id == Add) {
  std::vector<wchar_t> buffer(1024*1024); OPENFILENAMEW dialog{sizeof(dialog)}; dialog.hwndOwner = window_; dialog.lpstrFilter = L"Media files\0*.mp4;*.mkv;*.mov;*.avi;*.webm;*.jpg;*.jpeg;*.png;*.webp;*.bmp;*.mp3;*.m4a;*.flac;*.wav\0All files\0*.*\0"; dialog.lpstrFile = buffer.data(); dialog.nMaxFile = static_cast<DWORD>(buffer.size()); dialog.Flags = OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
  if (GetOpenFileNameW(&dialog)) { Launch launch; std::wstring first(buffer.data()); auto next = buffer.data()+first.size()+1; if (!*next) launch.files.push_back(first); else for (;*next;next+=wcslen(next)+1) launch.files.push_back(fs::path(first)/next); add_files(launch); }
 } else if (id == Remove && !busy_ && selected() >= 0) { stop_preview(); jobs_.erase(jobs_.begin()+selected()); ListView_DeleteAllItems(list_); update_list(); update_buttons(); }
 else if (id == Clear && !busy_) { stop_preview(); std::erase_if(jobs_,[](auto& job) { return job.state == State::Completed; }); ListView_DeleteAllItems(list_); update_list(); update_buttons(); }
 else if (id == Edit) edit_selected();
 else if (id == Reset && selected() >= 0) { auto& job = jobs_[selected()]; job.override_settings = false; if (job.state != State::Completed) job.state = State::Queued; update_list(); update_buttons(); }
 else if (id == Open && selected() >= 0 && !jobs_[selected()].result.outputs.empty()) { auto directory = jobs_[selected()].result.outputs[0].path.parent_path(); ShellExecuteW(window_,L"open",directory.c_str(),nullptr,nullptr,SW_SHOWNORMAL); }
 else if (id == Run) execute(false); else if (id == Retry) execute(true); else if (id == Cancel) { if(busy_) cancel_=true; else { stop_preview(); text(PreviewStatus,tr(L"Cancelled")); } preview_cancel_ = true; }
 else if (id == ConcatStart) execute(false,true); else if (id == Snapshot) execute(false,false,true); else if (id == Extract) execute(false,false,false,true);
 else if (id == Browse) { Microsoft::WRL::ComPtr<IFileOpenDialog> dialog; if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog)))) { dialog->SetOptions(FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM); if (SUCCEEDED(dialog->Show(window_))) { Microsoft::WRL::ComPtr<IShellItem> item; dialog->GetResult(&item); PWSTR path; if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&path))) { text(Output,path); CoTaskMemFree(path); } } } }
 else if (id == SubtitleBrowse) { wchar_t buffer[32768]{}; OPENFILENAMEW dialog{sizeof(dialog)}; dialog.hwndOwner = window_; dialog.lpstrFilter = L"Text subtitles\0*.srt;*.ass;*.ssa\0"; dialog.lpstrFile = buffer; dialog.nMaxFile = 32768; dialog.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR; if (GetOpenFileNameW(&dialog)) { text(SubtitleExternal,buffer); check(SubtitleUseExternal,true); } }
 else if (id == Play) play(); else if (id == EffectPreview) preview(true);
 else if (id == MarkIn) text(Start,wide(time_text(position_))); else if (id == MarkOut) text(End,wide(time_text(position_)));
 else if (id == PlayRange) { seek(time_value(utf8(text(Start)))); if (!player_.exists()) play(); else player_.play(); selection_play_ = true; }
 else if (id == ConcatUp || id == ConcatDown) { int index = selected(), other = index+(id==ConcatUp ? -1 : 1); if (index >= 0 && other >= 0 && other < static_cast<int>(jobs_.size()) && !busy_) { std::swap(jobs_[index],jobs_[other]); update_list(); ListView_SetItemState(list_,other,LVIS_SELECTED | LVIS_FOCUSED,LVIS_SELECTED | LVIS_FOCUSED); } }
 else if (id == ClipSave && selected() >= 0) { auto& input = jobs_[selected()].request.inputs[0]; input.start = time_value(utf8(text(ClipStart))); input.end = time_value(utf8(text(ClipEnd))); if (input.end && input.end <= input.start) throw Error("Invalid clip range."); auto value=options(); input.video=value.video_track; input.audio=value.mute ? -1 : value.audio_tracks.empty() ? -2 : value.audio_tracks[0]; }
 else if (id == SettingsButton) { TabCtrl_SetCurSel(tabs_,5); change_tab(); }
 else if (id == SaveSettings) {
  if(editing_) throw Error("Finish per-file settings first."); auto value = options(); settings_.options = value; settings_.modern_menu = checked(ModernMenu); settings_.presets = utf8(text(Presets)); std::istringstream presets(settings_.presets); std::string entry; unsigned count=0;
  while (std::getline(presets,entry,',')) { double n = parse(wide(entry)); if (n <= 0 || n > 1000000 || ++count>20) throw Error("Invalid size preset."); } if(!count) throw Error("At least one size preset is required.");
  bool change = settings_.chinese != (choice(Language)==1); settings_.chinese = choice(Language)==1; save_settings(settings_);
  if (change) rebuild_language(); else { auto target=text(Target); SendMessageW(control(Target),CB_RESETCONTENT,0,0); std::istringstream stream(settings_.presets); while(std::getline(stream,entry,',')) SendMessageW(control(Target),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(wide(entry).c_str())); text(Target,target); }
 }
 else if (id == Register) { bool modern = checked(ModernMenu); service([modern] { return register_shell(executable_dir(),modern); }); }
 else if (id == Unregister) service([] { unregister_shell(executable_dir()); return "Context menus removed."; });
 else if (id == Hardware) service([this] { auto result = detect_hardware(engine_, nullptr, true); std::string message; for (auto& c : result) message += c.encoder + (c.available ? ": available\r\n" : ": unavailable\r\n"); return message; });
}
void Window::rebuild_language() {
 int index=selected(); auto value = options(); stop_preview(); for (auto child = GetWindow(window_,GW_CHILD);child;) { auto next = GetWindow(child,GW_HWNDNEXT); DestroyWindow(child); child = next; }
 for (auto& page : page_controls_) page.clear(); controls_.clear(); labels_.clear(); widths_.clear();
 if (font_) DeleteObject(font_); font_ = nullptr;
 settings_.options = value; build(); update_list(); if(index>=0 && index<static_cast<int>(jobs_.size())) ListView_SetItemState(list_,index,LVIS_SELECTED | LVIS_FOCUSED,LVIS_SELECTED | LVIS_FOCUSED);
}
void Window::shutdown() { cancel_ = probe_cancel_ = preview_cancel_ = true; player_.close(); if (batch_.joinable()) batch_.join(); if (probe_worker_.joinable()) probe_worker_.join(); if (preview_worker_.joinable()) preview_worker_.join(); if (service_worker_.joinable()) service_worker_.join(); }
LRESULT CALLBACK Window::focus_procedure(HWND window,UINT message,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data) {
 auto self = reinterpret_cast<Window*>(data);
 if (message == WM_SETFOCUS) for (int i=0;i<6;++i) if (GetParent(window)==self->pages_[i]) {
  RECT bounds; GetClientRect(self->pages_[i],&bounds); int height=MulDiv(bounds.bottom,96,self->dpi_);
  for (auto& c:self->page_controls_[i]) if(c.window==window) {
   if (c.y < self->scroll_[i]) self->scroll_[i] = c.y;
   else if (c.y+c.h > self->scroll_[i]+height-2*kPagePadding) self->scroll_[i] = c.y+c.h-height+2*kPagePadding;
   self->layout_page(i); break;
  } break;
 }
 return DefSubclassProc(window,message,w,l);
}
LRESULT CALLBACK Window::page_procedure(HWND window,UINT message,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data) {
 auto self = reinterpret_cast<Window*>(data);
 if (message == WM_COMMAND || message == WM_NOTIFY) return SendMessageW(self->window_,message,w,l);
 int index = 0; while (index<6 && self->pages_[index]!=window) ++index; if (index==6) return DefSubclassProc(window,message,w,l);
 if (message == WM_MOUSEWHEEL) { self->scroll_[index] = std::clamp(self->scroll_[index]-GET_WHEEL_DELTA_WPARAM(w)/WHEEL_DELTA*80,0,self->max_scroll_[index]); self->layout_page(index); return 0; }
 if (message == WM_VSCROLL) {
  RECT client; GetClientRect(window,&client); int page = std::max(1,MulDiv(client.bottom,96,self->dpi_));
  SCROLLINFO info{sizeof(info)}; info.fMask = SIF_ALL; GetScrollInfo(window,SB_VERT,&info);
  int next = self->scroll_[index];
  switch (LOWORD(w)) {
   case SB_LINEUP: next -= 32; break; case SB_LINEDOWN: next += 32; break;
   case SB_PAGEUP: next -= page; break; case SB_PAGEDOWN: next += page; break;
   case SB_THUMBTRACK: case SB_THUMBPOSITION: next = info.nTrackPos; break;
   case SB_TOP: next = 0; break; case SB_BOTTOM: next = self->max_scroll_[index]; break;
   default: break;
  }
  self->scroll_[index] = std::clamp(next,0,self->max_scroll_[index]); self->layout_page(index); return 0;
 }
 return DefSubclassProc(window,message,w,l);
}
LRESULT CALLBACK Window::procedure(HWND window,UINT message,WPARAM w,LPARAM l) {
 auto self = reinterpret_cast<Window*>(GetWindowLongPtrW(window,GWLP_USERDATA));
 if (message == WM_NCCREATE) { self = static_cast<Window*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); self->window_ = window; SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self)); }
 if (!self) return DefWindowProcW(window,message,w,l);
 try {
  if (message == WM_CREATE) { self->build(); return 0; }
  if (message == WM_SIZE) { self->size_ = {0,0,LOWORD(l),HIWORD(l)}; if (self->list_) self->layout(); return 0; }
  if (message == WM_GETMINMAXINFO) { auto info = reinterpret_cast<MINMAXINFO*>(l); info->ptMinTrackSize = {self->px(760),self->px(560)}; return 0; }
  if (message == WM_DPICHANGED) { self->dpi_ = HIWORD(w); auto rect = reinterpret_cast<RECT*>(l); auto value = self->options(); SetWindowPos(window,nullptr,rect->left,rect->top,rect->right-rect->left,rect->bottom-rect->top,SWP_NOZORDER); self->settings_.options = value; self->rebuild_language(); return 0; }
  if (message == WM_COMMAND) {
   if ((LOWORD(w)==Start || LOWORD(w)==End) && HIWORD(w)==EN_CHANGE && self->media_ && self->media_->duration>0) {
    try { auto start=time_value(utf8(self->text(Start))),end=time_value(utf8(self->text(End))); if(end<=0) end=self->media_->duration; SendMessageW(self->control(Timeline),TBM_SETSELSTART,FALSE,static_cast<LPARAM>(start/self->media_->duration*100000)); SendMessageW(self->control(Timeline),TBM_SETSELEND,TRUE,static_cast<LPARAM>(end/self->media_->duration*100000)); }catch(...){} return 0;
   }
   if (HIWORD(w) == BN_CLICKED || HIWORD(w) == 0) self->command(LOWORD(w)); return 0;
  }
  if (message == WM_NOTIFY) { auto info = reinterpret_cast<NMHDR*>(l);
   if (info->hwndFrom == self->tabs_ && info->code == TCN_SELCHANGE) self->change_tab();
   if (info->hwndFrom == self->list_ && info->code == LVN_ITEMCHANGED && !self->suppress_selection_) { auto change = reinterpret_cast<NMLISTVIEW*>(l); if ((change->uNewState & LVIS_SELECTED) && !(change->uOldState & LVIS_SELECTED)) self->select_file(); }
   if (info->hwndFrom == self->control(AudioTracks) && info->code == LVN_ITEMCHANGED && !self->loading_) self->audio_dirty_ = true;
   return 0;
  }
  if (message == WM_HSCROLL && reinterpret_cast<HWND>(l) == self->control(Timeline) && self->media_) {
   auto position = static_cast<double>(SendMessageW(self->control(Timeline),TBM_GETPOS,0,0))/100000*self->media_->duration; self->position_ = position; self->seek_target_ = position; self->seeking_ = LOWORD(w) == TB_THUMBTRACK; self->pending_seek_ = true;
   SetPropW(window,L"SeekDue",reinterpret_cast<HANDLE>(GetTickCount64()+250)); self->text(Clock,wide(time_text(position))); return 0;
  }
  if (message == WM_DROPFILES) { auto drop = reinterpret_cast<HDROP>(w); UINT count = DragQueryFileW(drop,0xffffffff,nullptr,0); Launch launch; for (UINT i=0;i<count;++i) { UINT length = DragQueryFileW(drop,i,nullptr,0); std::wstring path(length+1,0); DragQueryFileW(drop,i,path.data(),length+1); path.resize(length); launch.files.emplace_back(path); } DragFinish(drop); self->add_files(launch); return 0; }
  if (message == LAUNCH_EVENT) { std::unique_ptr<Launch> launch(reinterpret_cast<Launch*>(l)); self->add_files(*launch); ShowWindow(window,SW_RESTORE); SetForegroundWindow(window); return 0; }
  if (message == UI_EVENT) { self->on_event(std::unique_ptr<UiEvent>(reinterpret_cast<UiEvent*>(l))); return 0; }
  if (message == PLAYER_EVENT) {
   if (!self->player_.accepts(l)) return 0;
   if (w == MF_MEDIA_ENGINE_EVENT_ERROR) { self->player_.close(); if (!self->proxy_) self->preview(false); else self->text(PreviewStatus,L"Media Foundation playback unavailable; processing remains available."); }
   if (w == MF_MEDIA_ENGINE_EVENT_LOADEDMETADATA) {
    try { self->player_.ready(); self->player_.seek(self->proxy_ ? 0 : self->position_); self->player_.resize(); }
    catch (const std::exception& error) { self->player_.close(); self->text(PreviewStatus,wide(error.what())); }
   }
   return 0;
  }
  if (message == WM_TIMER) { self->timer(); return 0; }
  if (message == WM_CLOSE) {
   if (self->busy_) { if (MessageBoxW(window,self->settings_.chinese ? L"取消失败中的任务并关闭？" : L"Cancel the running batch and close?",L"Convertii",MB_YESNO | MB_ICONQUESTION) == IDYES) { self->closing_ = true; self->cancel_ = true; } return 0; }
   self->shutdown(); try { self->settings_.options = self->options(); save_settings(self->settings_); } catch (...) {} DestroyWindow(window); return 0;
  }
  if (message == WM_DESTROY) { KillTimer(window,1); PostQuitMessage(0); return 0; }
 } catch (const std::exception& e) { MessageBoxW(window,wide(e.what()).c_str(),L"Convertii",MB_OK | MB_ICONERROR); return 0; }
 return DefWindowProcW(window,message,w,l);
}
}
