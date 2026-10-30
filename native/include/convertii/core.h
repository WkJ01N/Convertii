#pragma once
#include <convertii/platform.h>
#include <optional>

namespace cvt {
enum class Kind { Video, Image, Audio };
enum class Action { Compress, Convert, Remux, Snapshot, ExtractSubtitles, Concat };
enum class State { Queued, Probing, Running, Confirmation, Completed, Failed, Cancelled };
enum class Confirmation { None, Size, Hdr, Alpha, Concat, SubtitleStyles };
struct Track {
 int index = -1; std::string type, codec, language, title, time_base, pixel, profile;
 bool preferred = false, attached = false, text_subtitle = false;
 int width = 0, height = 0, rate = 0, channels = 0; double fps = 0, duration = 0;
 Json metadata;
};
struct Media {
 fs::path path; Kind kind = Kind::Video; uint64_t bytes = 0; double duration = 0;
 int width = 0, height = 0, video = -1, audio = -1; bool hdr = false, alpha = false, animated = false;
 std::vector<Track> tracks;
 const Track* track(int index) const;
};
struct Input { fs::path path; double start = 0, end = 0; int video = -1, audio = -2; };
struct Options {
 Action action = Action::Compress; bool target_mode = true; double target_mb = 20; int quality = 1;
 std::string video_format = "mp4", image_format = "webp", audio_format = "m4a", encoder = "auto";
 Kind output_kind = Kind::Video; int video_track = -1; std::vector<int> audio_tracks, subtitle_tracks;
 bool mute = false; int audio_kbps = 96, sample_rate = 48000, channels = 2, video_kbps = 0;
 int height = 0, width = 0; double fps = 0, start = 0, end = 0, speed = 1, volume_db = 0;
 int crop_x = 0, crop_y = 0, crop_w = 0, crop_h = 0, rotation = 0;
 bool hflip = false, vflip = false, gray = false, deinterlace = false, denoise = false, sharpen = false;
 double brightness = 0, contrast = 1, saturation = 1;
 bool allow_sdr = false, allow_alpha = false, allow_style_loss = false, fast_trim = false;
 std::string background = "FFFFFF"; fs::path output_directory;
 fs::path external_subtitle; int burn_subtitle = -1; bool burn_external = false, mux_external = false;
 double subtitle_offset = 0; std::string subtitle_format = "srt";
 bool normalize_concat = false, confirm_concat = false;
 double snapshot_time = 0;
 uint64_t target_bytes() const;
 void validate() const;
 bool video_effects() const;
 bool audio_effects() const;
};
struct Request { std::vector<Input> inputs; Options options; };
struct Artifact { fs::path path; uint64_t bytes = 0; };
struct Result { State state = State::Failed; std::string message; std::vector<Artifact> outputs; Confirmation confirmation = Confirmation::None; };
struct Progress { double fraction; std::string message; };
using ProgressCallback = std::function<void(const Progress&)>;
struct Plan {
 double duration = 0; std::string extension, video_encoder, audio_encoder;
 int64_t video_bps = 0; bool strict = false, two_pass = false, copy = false;
 std::vector<int> audios; int video = -1;
};
Media probe(const Engine& engine, const fs::path& path, const std::atomic_bool* cancel = nullptr);
Plan plan(const Media& media, const Options& options);
Result process(const Engine& engine, const Request& request, const std::atomic_bool* cancel = nullptr, ProgressCallback progress = {});
struct Capability { std::string encoder; bool available; std::string detail; };
std::vector<Capability> detect_hardware(const Engine& engine, const std::atomic_bool* cancel = nullptr);
std::string concat_incompatibility(const std::vector<Media>& media, const std::vector<Input>& inputs);
double time_value(const std::string& text);
std::string time_text(double seconds);
Json to_json_options(const Options& options);
Options from_json_options(const Json& json);
struct Settings { int schema = 2; bool chinese = false, modern_menu = true; std::string presets = "10,20,50,100"; Options options; };
Settings load_settings(const fs::path& root = data_root());
void save_settings(const Settings& settings, const fs::path& root = data_root());
// Timeline adjustment is shared by subtitle extraction, muxing and burn-in.
std::string transform_subtitles(const std::string& text, bool ass, double start, double end, double speed, double offset);
struct PreviewSpec { fs::path file; double source_start = 0, speed = 1; bool effects = false; };
PreviewSpec render_preview(const Engine& engine, const Input& input, const Options& options, double position,
 bool effects, const std::atomic_bool* cancel);
void clean_preview_cache(uint64_t limit = 512ull * 1024 * 1024);
fs::path preview_cache_root();
}
