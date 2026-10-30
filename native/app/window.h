#pragma once
#include <convertii/core.h>
#include <convertii/install.h>
#include "player.h"
#include <commctrl.h>
#include <thread>
#include <memory>
#include <map>

namespace cvt {
struct Launch { std::vector<fs::path> files; std::optional<Action> action; bool quick = false; };
Launch parse_launch(const std::vector<std::wstring>& args);
class Broker {
 Handle mutex_; std::thread listener_; std::atomic_bool stop_{false}; std::wstring pipe_; HWND window_ = nullptr;
public:
 bool primary = false;
 Broker(); ~Broker(); bool send(const Launch& launch); void listen(HWND window);
};
constexpr UINT LAUNCH_EVENT = WM_APP + 1;
struct Job { Request request; State state = State::Queued; std::string detail; double fraction = 0; Result result; bool override_settings = false; };
// A page element either sits on the two column grid (col/row/span) or at a fixed spot (grid = false).
struct Control { HWND window = nullptr; int col = 0, row = 0, span = 1; int width = 0, height = 0; bool label = false, grid = true; int x = 0, y = 0, w = 0, h = 0; bool flush = false, frame = false; int line = 0; };
struct UiEvent { enum class Type { Progress, Result, Done, Probe, Preview, Error, Info } type; size_t index = 0; unsigned generation = 0; int origin = 0; Progress progress{}; Result result; std::optional<Media> media; PreviewSpec preview; std::string message; };
class Window {
 friend struct UiTestAccess;
 HINSTANCE instance_; HWND window_ = nullptr, list_ = nullptr, tabs_ = nullptr, output_ = nullptr, status_ = nullptr;
 HWND pages_[6]{}; std::vector<Control> page_controls_[6]; int scroll_[6]{}, scroll_x_[6]{}, max_scroll_[6]{};
 HFONT font_ = nullptr;
 int label_height_ = 16, label_offset_ = 0, list_width_ = 0;
 int dpi_ = 96, page_ = 0; RECT size_{0,0,0,0}; bool compact_ = false;
 std::map<int,HWND> controls_; std::map<int,std::wstring> labels_; std::map<int,int> widths_; std::vector<Job> jobs_; std::optional<Media> media_;
 Settings settings_; Engine engine_; Player player_;
 std::thread batch_, probe_worker_, preview_worker_, service_worker_; std::atomic_bool cancel_{false}, probe_cancel_{false}, preview_cancel_{false};
 bool busy_ = false, closing_ = false, editing_ = false, proxy_ = false, effects_ = false, selection_play_ = false, suppress_selection_ = false;
 bool seeking_ = false, pending_seek_ = false; double proxy_start_ = 0, proxy_speed_ = 1, position_ = 0, seek_target_ = 0;
 bool audio_dirty_ = false, loading_ = false, preview_busy_ = false;
 unsigned probe_generation_ = 0, preview_generation_ = 0;
 Options global_before_edit_; size_t editing_index_ = 0; fs::path playing_file_;
 HWND control(int id) const; int px(int n) const; int selected() const;
 std::wstring text(int id) const; void text(int id,const std::wstring& value); bool checked(int id) const; void check(int id,bool value);
 int choice(int id) const; void choice(int id,int index); std::wstring tr(const wchar_t* value) const;
 HWND create(const wchar_t* type,const std::wstring& title,DWORD style,HWND parent,int id,int x,int y,int w,int h,DWORD ex = 0);
 int measure(const std::wstring& value,HFONT font) const;
 HWND make_button(HWND parent,int id,const wchar_t* label,bool checkbox = false,bool primary = false);
 void field(int page,int id,const wchar_t* label,int col,int row,int span = 1,const wchar_t* type = L"EDIT",const std::vector<std::wstring>& entries = {},const wchar_t* cue = nullptr);
 void button(int page,int id,const wchar_t* label,int col,int row,int span = 1,bool checkbox = false,bool primary = false);
 void build(); void layout(); void layout_page(int index); void change_tab(); void rebuild_language();
 Options options() const; void load_options(const Options& options); void add_files(const Launch& launch); void select_file();
 void update_list(); void update_buttons(); void execute(bool retry,bool concat = false, bool snapshot = false, bool subtitle = false);
 void on_event(std::unique_ptr<UiEvent> event); void edit_selected(); void preview(bool effects); void stop_preview(); void play();
 void service(std::function<std::string()> action); void command(int id); void timer(); void seek(double position); void shutdown();
 static LRESULT CALLBACK procedure(HWND,UINT,WPARAM,LPARAM); static LRESULT CALLBACK page_procedure(HWND,UINT,WPARAM,LPARAM,UINT_PTR,DWORD_PTR);
 static LRESULT CALLBACK focus_procedure(HWND,UINT,WPARAM,LPARAM,UINT_PTR,DWORD_PTR);
public:
 // A quick window drops the queue, the tool bar and the page tabs and shows only
 // the conversion page, which is what a shell menu entry needs.
 Window(HINSTANCE instance,Settings settings,bool quick = false); ~Window(); HWND open(int show); HWND hwnd() const { return window_; }
 void receive(const Launch& launch) { add_files(launch); }
};
}
