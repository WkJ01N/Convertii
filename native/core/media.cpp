#include <convertii/core.h>
#include <sstream>
#include <fstream>
#include <cmath>
#include <set>
#include <mutex>
#include <iomanip>

namespace cvt {
static double real(const Json& value) { try { return value.is_number() ? value.get<double>() : std::stod(value.get<std::string>()); } catch (...) { return 0; } }
static double duration(const Json& stream) {
 auto value = real(stream.value("duration", Json{}));
 if (value > 0) return value;
 try { return time_value(stream.value("tags", Json::object()).value("DURATION", "")); } catch (...) { return 0; }
}
static double rational(const std::string& value) { auto pos = value.find('/'); try { double a = std::stod(value), b = pos == std::string::npos ? 1 : std::stod(value.substr(pos + 1)); return b ? a / b : 0; } catch (...) { return 0; } }
const Track* Media::track(int index) const { for (auto& item : tracks) if (item.index == index) return &item; return nullptr; }
double time_value(const std::string& text) {
 if (text.empty()) return 0; std::istringstream in(text); std::string part; double value = 0; unsigned count = 0;
 while (std::getline(in, part, ':')) { size_t used = 0; auto number = std::stod(part, &used); if (used != part.size() || !std::isfinite(number) || number < 0 || ++count > 3) throw Error("Invalid time value."); value = value * 60 + number; }
 return value;
}
std::string time_text(double seconds) {
 if (!std::isfinite(seconds) || seconds < 0) seconds = 0;
 auto ms = static_cast<int64_t>(std::llround(seconds * 1000)); std::ostringstream out;
 out << std::setfill('0') << std::setw(2) << ms / 3600000 << ':' << std::setw(2) << ms / 60000 % 60 << ':' << std::setw(2) << ms / 1000 % 60 << '.' << std::setw(3) << ms % 1000; return out.str();
}
Media probe(const Engine& engine, const fs::path& path, const std::atomic_bool* cancel) {
 if (!fs::is_regular_file(path)) throw Error("Input file does not exist.");
 auto result = run(engine.ffprobe, {L"-v",L"error",L"-protocol_whitelist",L"file,pipe",L"-print_format",L"json",L"-show_format",L"-show_streams",L"-show_data_hash",L"sha256",L"-i",fs::absolute(path).wstring()}, cancel, {}, 60000);
 if (result.code) throw Error("Cannot read media: " + result.error.substr(0, 1500));
 auto json = Json::parse(result.output); Media media; media.path = fs::absolute(path); media.bytes = fs::file_size(path);
 auto format = json.value("format", Json::object()); media.duration = real(format.value("duration", Json{}));
 for (auto& stream : json.value("streams", Json::array())) {
  Track t; t.metadata = stream; t.index = stream.value("index", -1); t.type = stream.value("codec_type", ""); t.codec = stream.value("codec_name", "");
  t.time_base = stream.value("time_base", ""); t.profile = stream.value("profile", ""); t.width = stream.value("width", 0); t.height = stream.value("height", 0); t.pixel = stream.value("pix_fmt", "");
  auto tags = stream.value("tags", Json::object()); t.language = tags.value("language", ""); t.title = tags.value("title", "");
  auto disposition = stream.value("disposition", Json::object()); t.preferred = disposition.value("default", 0) != 0; t.attached = disposition.value("attached_pic", 0) != 0;
  t.rate = static_cast<int>(real(stream.value("sample_rate", Json{}))); t.channels = stream.value("channels", 0); t.fps = rational(stream.value("avg_frame_rate", "0/1")); t.duration = duration(stream);
  t.text_subtitle = t.type == "subtitle" && (t.codec == "subrip" || t.codec == "ass" || t.codec == "ssa" || t.codec == "mov_text" || t.codec == "webvtt" || t.codec == "text");
  media.tracks.push_back(t);
 }
 for (auto& t : media.tracks) {
  if (t.type == "video" && !t.attached && (media.video < 0 || t.preferred)) media.video = t.index;
  if (t.type == "audio" && (media.audio < 0 || t.preferred)) media.audio = t.index;
 }
 if (media.video < 0) { if (media.audio < 0) throw Error("No supported video, audio or image stream."); media.kind = Kind::Audio; }
 else {
  auto& t = *media.track(media.video); media.width = t.width; media.height = t.height;
  auto container = format.value("format_name", "");
  bool image = container.find("image2") != std::string::npos || container.ends_with("_pipe") || container == "gif" || container == "apng";
  media.kind = image ? Kind::Image : Kind::Video;
  auto transfer = t.metadata.value("color_transfer", ""); media.hdr = transfer == "smpte2084" || transfer == "arib-std-b67";
  media.alpha = t.pixel.starts_with("rgba") || t.pixel.starts_with("bgra") || t.pixel.starts_with("argb") || t.pixel.starts_with("abgr") || t.pixel.starts_with("yuva") || t.pixel.starts_with("gbrap") || t.pixel.starts_with("ya") || t.pixel == "pal8";
  media.animated = image && (real(t.metadata.value("nb_frames", Json{})) > 1 || t.codec == "gif" || t.codec == "apng");
  if (image && t.codec == "webp") { std::ifstream f(path, std::ios::binary); unsigned char head[21]{}; f.read(reinterpret_cast<char*>(head), 21); if (f.gcount() == 21 && std::string(reinterpret_cast<char*>(head + 12), 4) == "VP8X" && (head[20] & 2)) media.animated = true; }
  if (image && t.codec == "png") {
   std::ifstream f(path, std::ios::binary); f.seekg(8); unsigned char head[8];
   while (f.read(reinterpret_cast<char*>(head), 8)) { std::string type(reinterpret_cast<char*>(head + 4), 4); if (type == "acTL") { media.animated = true; break; } if (type == "IDAT" || type == "IEND") break;
    uint64_t size = static_cast<uint64_t>(head[0]) << 24 | static_cast<uint64_t>(head[1]) << 16 | static_cast<uint64_t>(head[2]) << 8 | head[3]; if (size > media.bytes) break; f.seekg(size + 4, std::ios::cur); }
  }
 }
 auto video = media.track(media.video); auto audio = media.track(media.audio);
 if (video && video->duration > 0 && (!audio || audio->duration > 0)) media.duration = std::max(video->duration, audio ? audio->duration : 0);
 if (!video && audio && audio->duration > 0) media.duration = audio->duration;
 return media;
}
uint64_t Options::target_bytes() const { return static_cast<uint64_t>(target_mb * 1000000); }
void Options::validate() const {
 if(static_cast<int>(action)<0 || static_cast<int>(action)>5 || static_cast<int>(output_kind)<0 || static_cast<int>(output_kind)>2) throw Error("Invalid operation or output type.");
 if (!std::isfinite(target_mb) || target_mb <= 0 || target_mb > 1000000) throw Error("Target size must be between 0 and 1,000,000 MB.");
 if (quality < 0 || quality > 2 || height < 0 || width < 0 || height > 16384 || width > 16384 || !std::isfinite(fps) || fps < 0 || fps > 240 || video_kbps < 0 || video_kbps > 1000000 || audio_kbps < 16 || audio_kbps > 512) throw Error("Invalid encoding settings.");
 if (!std::isfinite(start) || !std::isfinite(end) || start < 0 || end < 0 || (end && end <= start) || !std::isfinite(speed) || speed < .25 || speed > 4) throw Error("Invalid trim range or speed (0.25-4).");
 if (crop_x < 0 || crop_y < 0 || crop_w < 0 || crop_h < 0 || ((crop_w == 0) != (crop_h == 0))) throw Error("Invalid crop rectangle.");
 if (rotation != 0 && rotation != 90 && rotation != 180 && rotation != 270) throw Error("Invalid rotation.");
 if (!std::isfinite(brightness) || std::abs(brightness) > 1 || !std::isfinite(contrast) || contrast < 0 || contrast > 3 || !std::isfinite(saturation) || saturation < 0 || saturation > 3 || !std::isfinite(volume_db) || std::abs(volume_db) > 60 || !std::isfinite(subtitle_offset) || std::abs(subtitle_offset) > 86400 || !std::isfinite(snapshot_time) || snapshot_time < 0) throw Error("Invalid filter value.");
 if (channels < 1 || channels > 8 || sample_rate < 8000 || sample_rate > 192000) throw Error("Invalid audio settings.");
 if (!std::set<std::string>{"mp4","mkv","webm"}.contains(video_format) || !std::set<std::string>{"jpeg","png","webp"}.contains(image_format) || !std::set<std::string>{"mp3","m4a","flac","wav"}.contains(audio_format)) throw Error("Unsupported output format.");
 if (background.size() != 6 || !std::all_of(background.begin(), background.end(), [](unsigned char c) { return std::isxdigit(c); })) throw Error("JPEG background must contain six hexadecimal digits.");
}
bool Options::video_effects() const { return width || height || fps || crop_w || rotation || hflip || vflip || gray || deinterlace || denoise || sharpen || brightness || contrast != 1 || saturation != 1 || speed != 1 || burn_subtitle >= 0 || burn_external; }
bool Options::audio_effects() const { return speed != 1 || volume_db != 0; }
Plan plan(const Media& media, const Options& o) {
 o.validate(); Plan p; p.video = o.video_track >= 0 ? o.video_track : media.video;
 p.audios = o.mute ? std::vector<int>{} : o.audio_tracks.empty() ? (media.audio >= 0 ? std::vector<int>{media.audio} : std::vector<int>{}) : o.audio_tracks;
 for (auto index : p.audios) if (!media.track(index) || media.track(index)->type != "audio") throw Error("Selected audio track does not exist in this file.");
 if (o.output_kind == Kind::Audio && p.audios.empty()) throw Error("No audio track selected.");
 if (o.output_kind != Kind::Audio && p.video < 0) throw Error("No video/image track available.");
 if (p.video >= 0 && (!media.track(p.video) || media.track(p.video)->type != "video")) throw Error("Selected video track does not exist.");
 double selected_duration = 0; bool known = true;
 if (p.video >= 0 && o.output_kind != Kind::Audio) { auto t=media.track(p.video); if (t && t->duration>0) selected_duration=t->duration; else known=false; }
 for (int index:p.audios) { auto t=media.track(index); if(t && t->duration>0) selected_duration=std::max(selected_duration,t->duration); else known=false; }
 if (!known || selected_duration<=0) selected_duration=media.duration;
 p.duration = (std::min(o.end > 0 ? o.end : selected_duration, selected_duration) - o.start) / o.speed;
 if (media.kind != Kind::Image && (p.duration <= 0 || !std::isfinite(p.duration))) throw Error("Invalid or unavailable duration / trim range.");
 p.extension = o.action == Action::Snapshot || media.kind == Kind::Image ? (o.image_format == "jpeg" ? "jpg" : o.image_format) : o.output_kind == Kind::Audio ? o.audio_format : o.video_format;
 p.copy = o.action == Action::Remux || o.fast_trim;
 if (p.copy && ((o.output_kind!=Kind::Audio && o.video_effects()) || o.audio_effects() || (o.output_kind!=Kind::Audio && media.hdr && o.allow_sdr))) throw Error("Stream copy cannot apply filters or speed changes. Switch to re-encoding.");
 p.strict = o.action == Action::Compress && o.target_mode;
 if (p.copy && p.strict) throw Error("Stream copy cannot guarantee a target size. Switch to re-encoding.");
 p.video_encoder = o.video_format == "webm" ? "libvpx-vp9" : "libx264";
 p.audio_encoder = o.output_kind == Kind::Audio ? (o.audio_format == "mp3" ? "libmp3lame" : o.audio_format == "flac" ? "flac" : o.audio_format == "wav" ? "pcm_s16le" : "aac") : o.video_format == "webm" ? "libopus" : "aac";
 p.video_bps = static_cast<int64_t>(o.video_kbps) * 1000;
 if (p.strict && media.kind == Kind::Video && o.output_kind == Kind::Video) {
  p.video_bps = static_cast<int64_t>((static_cast<double>(o.target_bytes()) * .96 - 4096) * 8 / p.duration) - static_cast<int64_t>(o.audio_kbps) * 1000 * p.audios.size();
  if (p.video_bps < 20000) throw Error("Target is too small for the edited duration and audio tracks. Increase target or change settings.");
  p.two_pass = true;
 }
 return p;
}
std::vector<Capability> detect_hardware(const Engine& engine, const std::atomic_bool* cancel, bool refresh) {
 // Every probe starts FFmpeg for a one-frame encode, so probing again for each job in a batch
 // is wasted work. Cache the answer per engine binary; "refresh" re-probes on demand.
 static std::mutex gate; static std::string cached_engine; static std::vector<Capability> cached;
 auto engine_key = utf8(engine.ffmpeg.wstring());
 { std::lock_guard lock(gate); if (!refresh && !cached.empty() && cached_engine == engine_key) return cached; }
 std::vector<Capability> result;
 for (auto encoder : {"h264_nvenc","h264_qsv","h264_amf"}) {
  try { auto test = run(engine.ffmpeg, {L"-v",L"error",L"-f",L"lavfi",L"-i",L"color=size=128x128:rate=1",L"-frames:v",L"1",L"-c:v",wide(encoder),L"-f",L"null",L"-"}, cancel, {}, 12000); result.push_back({encoder,test.code == 0,test.error}); }
  catch (const Cancelled&) { throw; } catch (const std::exception& e) { result.push_back({encoder,false,e.what()}); }
 }
 { std::lock_guard lock(gate); cached_engine = engine_key; cached = result; }
 return result;
}
std::string concat_incompatibility(const std::vector<Media>& media, const std::vector<Input>& inputs) {
 if (media.size() < 2) return "At least two files are required.";
 std::string signature;
 for (size_t i = 0; i < media.size(); ++i) {
  auto& m = media[i]; auto v = m.track(inputs[i].video >= 0 ? inputs[i].video : m.video); auto a = m.track(inputs[i].audio == -2 ? m.audio : inputs[i].audio);
  if (!v || m.kind != Kind::Video) return "All concat inputs must contain video.";
  Json sig = Json::array({v->codec,v->width,v->height,v->pixel,v->profile,v->time_base,v->metadata.value("extradata_hash", ""),v->metadata.value("sample_aspect_ratio", ""),v->fps});
  if (a) sig.push_back(Json::array({a->codec,a->rate,a->channels,a->time_base,a->metadata.value("channel_layout", ""),a->metadata.value("extradata_hash", "")})); else sig.push_back(nullptr);
  if (!i) signature = sig.dump(); else if (signature != sig.dump()) return "Video/audio parameters differ at file " + std::to_string(i + 1) + ". Confirm normalized re-encoding.";
 }
 return {};
}
}
