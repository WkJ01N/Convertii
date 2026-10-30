#include <convertii/core.h>

namespace cvt {
#define OPTION_FIELDS(X) X(action) X(target_mode) X(target_mb) X(quality) X(video_format) X(image_format) X(audio_format) X(encoder) X(output_kind) X(video_track) X(audio_tracks) X(subtitle_tracks) X(mute) X(audio_kbps) X(sample_rate) X(channels) X(video_kbps) X(height) X(width) X(fps) X(start) X(end) X(speed) X(volume_db) X(crop_x) X(crop_y) X(crop_w) X(crop_h) X(rotation) X(hflip) X(vflip) X(gray) X(deinterlace) X(denoise) X(sharpen) X(brightness) X(contrast) X(saturation) X(allow_sdr) X(allow_alpha) X(allow_style_loss) X(fast_trim) X(background) X(burn_subtitle) X(burn_external) X(mux_external) X(subtitle_offset) X(subtitle_format) X(normalize_concat) X(confirm_concat) X(snapshot_time)
Json to_json_options(const Options& o) {
 Json j = Json::object();
#define SAVE(name) j[#name] = o.name;
 OPTION_FIELDS(SAVE)
#undef SAVE
 j["output_directory"] = utf8(o.output_directory.wstring()); j["external_subtitle"] = utf8(o.external_subtitle.wstring()); return j;
}
Options from_json_options(const Json& j) {
 Options o;
#define LOAD(name) if (j.contains(#name)) j.at(#name).get_to(o.name);
 OPTION_FIELDS(LOAD)
#undef LOAD
 o.output_directory = wide(j.value("output_directory", "")); o.external_subtitle = wide(j.value("external_subtitle", "")); o.validate(); return o;
}
Settings load_settings(const fs::path& root) {
 Settings settings; settings.chinese = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_CHINESE;
 auto path = root / L"settings.json"; if (!fs::exists(path)) return settings;
 try {
  auto j = Json::parse(read_text(path));
   if (j.value("schema", 0) >= 2) {
    settings.chinese = j.value("language", "en-US") == "zh-CN"; settings.modern_menu = j.value("modern_menu", true);
    settings.presets = j.value("presets", "10,20,50,100"); settings.options = from_json_options(j.value("options", Json::object()));
    if (j.contains("window") && j["window"].is_object()) {
     const auto& w = j["window"]; settings.placement.x = w.value("x", 0); settings.placement.y = w.value("y", 0);
     settings.placement.width = w.value("width", 0); settings.placement.height = w.value("height", 0);
     settings.placement.maximized = w.value("maximized", false); settings.placement.tab = w.value("tab", 0);
    }
   } else {
   settings.chinese = j.value("Language", "en-US") == "zh-CN"; settings.modern_menu = j.value("ModernMenu", true); settings.presets = j.value("SizePresets", "10,20,50,100"); auto old = j.value("Options", Json::object()); auto& o = settings.options;
   o.action = old.value("Operation", 0) == 1 ? Action::Convert : Action::Compress; o.target_mode = old.value("Mode", 0) == 0; o.target_mb = old.value("TargetMegabytes", 20.); o.quality = old.value("Quality", 1);
   o.video_format = old.value("VideoFormat", "mp4"); o.image_format = old.value("ImageFormat", "webp"); o.height = old.value("MaxHeight", 0); o.fps = old.value("FrameRate", 0.); o.video_kbps = old.value("VideoKbps", 0); o.audio_kbps = old.value("AudioKbps", 96); o.mute = old.value("Mute", false); o.allow_sdr = old.value("AllowSdr", false); o.allow_alpha = old.value("AllowFlatten", false); o.background = old.value("Background", "FFFFFF");
   if (old.contains("OutputDirectory") && old["OutputDirectory"].is_string()) o.output_directory = wide(old["OutputDirectory"].get<std::string>());
   const char* encoders[] = {"auto","software","nvidia","intel","amd"}; int choice = old.value("Encoder", 0); if (choice >= 0 && choice < 5) o.encoder = encoders[choice]; o.validate();
   auto backup = root / L"settings.v1.bak"; if (!fs::exists(backup)) fs::copy_file(path, backup); save_settings(settings, root);
  }
 } catch (...) { /* Keep invalid originals for repair, but allow a clean launch. */ }
 return settings;
}
void save_settings(const Settings& settings, const fs::path& root) {
 auto placement = Json{{"x",settings.placement.x},{"y",settings.placement.y},{"width",settings.placement.width},{"height",settings.placement.height},{"maximized",settings.placement.maximized},{"tab",settings.placement.tab}};
 atomic_write(root / L"settings.json", Json{{"schema",3},{"language",settings.chinese ? "zh-CN" : "en-US"},{"modern_menu",settings.modern_menu},{"presets",settings.presets},{"options",to_json_options(settings.options)},{"window",placement}}.dump(2));
}
}
