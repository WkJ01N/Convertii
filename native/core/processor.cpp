#include <convertii/core.h>
#include <fstream>
#include <sstream>
#include <cmath>
#include <set>

namespace cvt {
using Args = std::vector<std::wstring>;
static std::wstring number(double value) { std::wostringstream out; out.imbue(std::locale::classic()); out.precision(12); out << value; return out.str(); }
static std::string num(double value) { return utf8(number(value)); }
static void add(Args& args, std::initializer_list<std::wstring> values) { args.insert(args.end(), values.begin(), values.end()); }
static void ensure(const ProcessResult& result) { if (result.code) throw Error("FFmpeg failed: " + result.error.substr(result.error.size() > 2000 ? result.error.size() - 2000 : 0)); }
static Args base(const fs::path& input) { return {L"-hide_banner",L"-nostdin",L"-v",L"error",L"-y",L"-progress",L"pipe:1",L"-nostats",L"-protocol_whitelist",L"file,pipe",L"-i",input.wstring()}; }
static std::string join(const std::vector<std::string>& list, const char* separator = ",") { std::string out; for (auto& value : list) { if (!out.empty()) out += separator; out += value; } return out; }
struct Workspace {
 fs::path path;
 explicit Workspace(const fs::path& root) { fs::create_directories(root); path = root / wide(".convertii-" + unique_id()); fs::create_directory(path); }
 ~Workspace() { std::error_code e; fs::remove_all(path, e); }
};
static std::pair<int,int> displayed_size(const Media& media, int index) {
 auto track = media.track(index >= 0 ? index : media.video);
 if (!track) return {media.width, media.height};
 int w = track->width, h = track->height; double sar = 1;
 auto ratio = track->metadata.value("sample_aspect_ratio", "1:1");
 try { auto colon = ratio.find(':'); if (colon != std::string::npos) { auto denominator = std::stod(ratio.substr(colon+1)); if (denominator > 0) sar = std::stod(ratio) / denominator; } } catch (...) {}
 if (!std::isfinite(sar) || sar <= 0) sar = 1;
 for (auto& side : track->metadata.value("side_data_list", Json::array())) {
  if (std::abs(side.value("rotation", 0)) % 180 == 90) { std::swap(w,h); sar = 1/sar; break; }
 }
 return {std::max(1, static_cast<int>(std::round(w*sar))), h};
}
static std::vector<std::string> video_filters(const Media& m, const Options& o, const std::string& subtitle = {}) {
 std::vector<std::string> filters;
 if (m.kind != Kind::Image) { std::string trim = "trim=start=" + num(o.start); if (o.end > 0) trim += ":end=" + num(o.end); filters.push_back(trim); filters.push_back("setpts=PTS-STARTPTS"); }
 // Autorotation runs in FFmpeg's input stage. Normalize display pixels before
 // crop/resize so setting SAR=1 later cannot stretch anamorphic sources.
 filters.push_back("scale=w='max(1,round(iw*sar))':h=ih,setsar=1");
 if (o.crop_w) {
  auto [w,h] = displayed_size(m,o.video_track);
  if (o.crop_x + o.crop_w > w || o.crop_y + o.crop_h > h) throw Error("Crop rectangle exceeds the displayed frame.");
  filters.push_back("crop=" + std::to_string(o.crop_w) + ':' + std::to_string(o.crop_h) + ':' + std::to_string(o.crop_x) + ':' + std::to_string(o.crop_y));
 }
 if (o.rotation == 90) filters.push_back("transpose=clock");
 if (o.rotation == 180) { filters.push_back("hflip"); filters.push_back("vflip"); }
 if (o.rotation == 270) filters.push_back("transpose=cclock");
 if (o.hflip) filters.push_back("hflip"); if (o.vflip) filters.push_back("vflip");
 if (o.width || o.height) {
  if (o.width && o.height) filters.push_back("scale='min(iw," + std::to_string(o.width) + ")':'min(ih," + std::to_string(o.height) + ")':force_original_aspect_ratio=decrease:force_divisible_by=2");
  else if (o.width) filters.push_back("scale='min(iw," + std::to_string(o.width) + ")':-2");
  else filters.push_back("scale=-2:'min(ih," + std::to_string(o.height) + ")'");
 }
 if (m.hdr && o.allow_sdr) filters.push_back("zscale=t=linear:npl=100,format=gbrpf32le,zscale=p=bt709,tonemap=tonemap=hable:desat=0,zscale=t=bt709:m=bt709:r=tv,format=yuv420p");
 if (o.deinterlace) filters.push_back("yadif"); if (o.denoise) filters.push_back("hqdn3d"); if (o.sharpen) filters.push_back("unsharp=5:5:0.8");
 if (o.brightness || o.contrast != 1 || o.saturation != 1) filters.push_back("eq=brightness=" + num(o.brightness) + ":contrast=" + num(o.contrast) + ":saturation=" + num(o.saturation));
 if (o.gray) filters.push_back("hue=s=0");
 if (!subtitle.empty()) filters.push_back("subtitles=filename='" + subtitle + "'");
 if (o.speed != 1) filters.push_back("setpts=PTS/" + num(o.speed));
 if (o.fps) filters.push_back("fps=" + num(o.fps));
 if (m.kind != Kind::Image && o.action != Action::Snapshot) filters.push_back("scale=trunc(iw/2)*2:trunc(ih/2)*2,setsar=1");
 return filters;
}
static std::vector<std::string> audio_filters(const Options& o) {
 std::vector<std::string> filters; std::string trim = "atrim=start=" + num(o.start); if (o.end > 0) trim += ":end=" + num(o.end); filters.push_back(trim); filters.push_back("asetpts=PTS-STARTPTS");
 double speed = o.speed; while (speed > 2) { filters.push_back("atempo=2"); speed /= 2; } while (speed < .5) { filters.push_back("atempo=0.5"); speed /= .5; } if (speed != 1) filters.push_back("atempo=" + num(speed));
 if (o.volume_db) filters.push_back("volume=" + num(o.volume_db) + "dB"); return filters;
}
struct SubInput { fs::path path; std::string codec; };
static std::vector<SubInput> prepare_subtitles(const Engine& engine, const Media& media, const Options& o,
 const fs::path& workspace, const std::atomic_bool* cancel, bool burn) {
 std::vector<std::pair<int,fs::path>> sources;
 if (burn) { if (o.burn_external) sources.push_back({-1,o.external_subtitle}); else if (o.burn_subtitle >= 0) sources.push_back({o.burn_subtitle,media.path}); }
 else { for (auto index : o.subtitle_tracks) sources.push_back({index,media.path}); if (o.mux_external) sources.push_back({-1,o.external_subtitle}); }
 std::vector<SubInput> result;
 for (auto& [index,path] : sources) {
  if (path.empty() || !fs::exists(path)) throw Error("Subtitle file does not exist.");
  bool ass = false;
  if (index >= 0) { auto t = media.track(index); if (!t || !t->text_subtitle) throw Error("Only text subtitle tracks are supported (SRT/ASS/mov_text/WebVTT)."); ass = t->codec == "ass" || t->codec == "ssa"; }
  else { auto ext = path.extension().wstring(); std::transform(ext.begin(), ext.end(), ext.begin(), towlower); if (ext != L".srt" && ext != L".ass" && ext != L".ssa") throw Error("External subtitles must be SRT or ASS."); ass = ext != L".srt"; }
  if (!burn && ass && o.action != Action::ExtractSubtitles && o.video_format != "mkv" && !o.allow_style_loss) throw Error("Subtitle style conversion needs confirmation.");
  if (o.action == Action::ExtractSubtitles && o.subtitle_format == "srt" && ass && !o.allow_style_loss) throw Error("Subtitle style conversion needs confirmation.");
  bool output_ass = o.action == Action::ExtractSubtitles ? o.subtitle_format == "ass" : ass;
  auto filename = "subtitle" + std::to_string(result.size()) + (output_ass ? ".ass" : ".srt");
  auto raw = workspace / wide("raw-" + filename); Args args = base(path); add(args, {L"-map", index >= 0 ? L"0:" + std::to_wstring(index) : L"0:s:0",L"-c:s", output_ass ? L"ass" : L"srt",raw.wstring()}); ensure(run(engine.ffmpeg, args, cancel));
  auto text = transform_subtitles(read_text(raw), output_ass, o.start, o.end > 0 ? std::min(o.end,media.duration) : media.duration, burn ? 1 : o.speed, o.subtitle_offset);
  auto adjusted = workspace / wide(filename); write_text(adjusted, text); result.push_back({adjusted, output_ass ? "ass" : "srt"});
 }
 return result;
}
static void validate_copy(const Media& media, const Options& o, const Plan& p) {
 if(o.output_kind==Kind::Audio) {
  for(int index:p.audios) { auto codec=media.track(index)->codec; bool compatible=o.audio_format=="m4a" ? codec=="aac" || codec=="alac" : o.audio_format=="mp3" ? codec=="mp3" : o.audio_format=="flac" ? codec=="flac" : codec.starts_with("pcm_"); if(!compatible) throw Error("Audio codec is incompatible with the chosen container. Switch to re-encoding."); }
  return;
 }
 auto v = media.track(p.video);
 if (o.video_format == "mp4" && v && !std::set<std::string>{"h264","hevc","av1","mpeg4","mjpeg"}.contains(v->codec)) throw Error("Video codec is incompatible with MP4 stream copy.");
 if (o.video_format == "webm" && v && v->codec != "vp8" && v->codec != "vp9" && v->codec != "av1") throw Error("Video codec is incompatible with WebM stream copy.");
 for (int index : p.audios) { auto codec = media.track(index)->codec;
  if (o.video_format == "webm" && codec != "opus" && codec != "vorbis") throw Error("Audio codec is incompatible with WebM stream copy.");
  if (o.video_format == "mp4" && !std::set<std::string>{"aac","mp3","ac3","eac3","alac","opus","flac"}.contains(codec)) throw Error("Audio codec is incompatible with MP4 stream copy."); }
}
static void progress_line(const std::string& line, double duration, double begin, double scale, ProgressCallback progress, const std::string& label) {
 if (!progress || !line.starts_with("out_time_us=")) return;
 try { double seconds = std::stod(line.substr(12)) / 1000000.; progress({std::clamp(begin + seconds / std::max(.001,duration) * scale, 0., .97),label}); } catch (...) {}
}
static void encode_av(const Engine& engine, const Media& media, const Options& o, const Plan& p,
 const fs::path& output, const fs::path& workspace, const std::atomic_bool* cancel, ProgressCallback progress) {
 bool audio_only = o.output_kind == Kind::Audio || media.kind == Kind::Audio;
 auto burn = audio_only ? std::vector<SubInput>{} : prepare_subtitles(engine, media, o, workspace, cancel, true);
 auto subs = audio_only ? std::vector<SubInput>{} : prepare_subtitles(engine, media, o, workspace, cancel, false);
 auto vf = audio_only ? std::vector<std::string>{} : video_filters(media, o, burn.empty() ? "" : utf8(burn[0].path.filename().wstring()));
 auto af = audio_filters(o); int passes = p.two_pass ? 2 : 1;
 for (int pass = 1; pass <= passes; ++pass) {
  check_cancel(cancel); bool first = passes == 2 && pass == 1; Args args = base(media.path);
  if (p.copy && o.start > 0) { auto input = std::find(args.begin(),args.end(),L"-i"); args.insert(input,{L"-ss",number(o.start)}); }
  for (auto& sub : subs) add(args,{L"-i",sub.path.wstring()});
  if (p.copy && o.end > 0) add(args,{L"-t",number(o.end - o.start)});
  if (!audio_only) add(args,{L"-map",L"0:" + std::to_wstring(p.video)});
  if (!first) for (int index : p.audios) add(args,{L"-map",L"0:" + std::to_wstring(index)});
  if (!first && !audio_only) for (size_t i = 0; i < subs.size(); ++i) add(args,{L"-map",std::to_wstring(i+1) + L":s:0"});
  add(args,{L"-map_metadata",L"-1",L"-map_chapters",L"-1",L"-dn"});
  if (p.copy) add(args,{L"-c",L"copy",L"-avoid_negative_ts",L"make_zero"});
  else {
   if (!audio_only) {
    add(args,{L"-c:v",wide(p.video_encoder),L"-pix_fmt",L"yuv420p"}); if (!vf.empty()) add(args,{L"-vf",wide(join(vf))});
    if (p.video_bps > 0) add(args,{L"-b:v",std::to_wstring(p.video_bps)});
    else { auto q = o.quality == 0 ? 20 : o.quality == 2 ? 30 : 24;
     if (p.video_encoder == "libx264" || p.video_encoder == "libvpx-vp9") { add(args,{L"-crf",std::to_wstring(q)}); if (p.video_encoder == "libvpx-vp9") add(args,{L"-b:v",L"0"}); }
     else if (p.video_encoder == "h264_nvenc") add(args,{L"-rc",L"vbr",L"-cq",std::to_wstring(q),L"-b:v",L"0"});
     else if (p.video_encoder == "h264_qsv") add(args,{L"-global_quality",std::to_wstring(q)});
     else add(args,{L"-rc",L"cqp",L"-qp_i",std::to_wstring(q),L"-qp_p",std::to_wstring(q)});
    }
    if (p.video_encoder == "libx264") add(args,{L"-preset",L"medium"}); if (p.video_encoder == "libvpx-vp9") add(args,{L"-deadline",L"good",L"-cpu-used",L"3"});
    if (media.hdr && o.allow_sdr && p.video_encoder == "libx264") add(args,{L"-x264-params",L"colorprim=bt709:transfer=bt709:colormatrix=bt709"});
   }
   if (!first && !p.audios.empty()) {
    add(args,{L"-c:a",wide(p.audio_encoder),L"-ar",std::to_wstring(o.sample_rate),L"-ac",std::to_wstring(o.channels),L"-af",wide(join(af))});
    if (p.audio_encoder != "flac" && p.audio_encoder != "pcm_s16le") add(args,{L"-b:a",std::to_wstring(o.audio_kbps) + L"k"});
   }
  }
  if (!first && !subs.empty() && !audio_only) add(args,{L"-c:s",o.video_format == "mp4" ? L"mov_text" : o.video_format == "webm" ? L"webvtt" : L"copy"});
  if (first || p.audios.empty()) add(args,{L"-an"});
  if (p.two_pass) add(args,{L"-pass",std::to_wstring(pass),L"-passlogfile",(workspace / L"pass").wstring()});
  if (first) add(args,{L"-f",L"null",L"-"});
  else { if ((o.video_format == "mp4" && !audio_only) || (audio_only && o.audio_format == "m4a")) add(args,{L"-movflags",L"+faststart"}); args.push_back(output.wstring()); }
  std::string label = "Encoding · pass " + std::to_string(pass) + '/' + std::to_string(passes); if (progress) progress({static_cast<double>(pass-1)/passes*.97,label});
  ensure(run(engine.ffmpeg,args,cancel,[=](const std::string& line) { progress_line(line,p.duration,static_cast<double>(pass-1)/passes*.97,.97/passes,progress,label); },0,workspace));
 }
}
static bool encode_image(const Engine& engine, const Media& media, const Options& o, const fs::path& output,
 const fs::path& workspace, const std::atomic_bool* cancel, ProgressCallback progress) {
 bool strict = o.action == Action::Compress && o.target_mode; auto image_options = o;
 if (o.action == Action::Snapshot) { image_options.start = 0; image_options.end = 0; image_options.speed = 1; image_options.fps = 0; }
 auto filters = video_filters(media,image_options);
 if (o.image_format == "jpeg" && media.alpha) {
  int r = std::stoi(o.background.substr(0,2),nullptr,16), g = std::stoi(o.background.substr(2,2),nullptr,16), b = std::stoi(o.background.substr(4,2),nullptr,16);
  filters.push_back("format=rgba,geq=r='r(X,Y)*alpha(X,Y)/255+" + std::to_string(r) + "*(1-alpha(X,Y)/255)':g='g(X,Y)*alpha(X,Y)/255+" + std::to_string(g) + "*(1-alpha(X,Y)/255)':b='b(X,Y)*alpha(X,Y)/255+" + std::to_string(b) + "*(1-alpha(X,Y)/255)':a=255,format=rgb24");
 }
 int low = 0, high = 100, trials = strict && o.image_format != "png" ? 8 : 1; bool found = false;
 auto candidate = workspace / (L"candidate." + output.extension().wstring().substr(1));
 for (int trial = 0; trial < trials && low <= high; ++trial) {
  int rank = strict ? (trial ? (low+high)/2 : 0) : o.quality == 0 ? 92 : o.quality == 2 ? 55 : 78;
  auto args = base(media.path); if (o.action == Action::Snapshot) add(args,{L"-ss",number(o.snapshot_time)});
  add(args,{L"-map",L"0:" + std::to_wstring(o.video_track >= 0 ? o.video_track : media.video),L"-frames:v",L"1",L"-an",L"-sn",L"-map_metadata",L"-1"});
  if (!filters.empty()) add(args,{L"-vf",wide(join(filters))});
  if (o.image_format == "jpeg") add(args,{L"-c:v",L"mjpeg",L"-q:v",std::to_wstring(31 - rank * 29 / 100),L"-pix_fmt",L"yuvj444p"});
  else if (o.image_format == "webp") add(args,{L"-c:v",L"libwebp",L"-quality",std::to_wstring(rank),L"-compression_level",L"6"});
  else add(args,{L"-c:v",L"png",L"-compression_level",L"9",L"-pix_fmt",media.alpha ? L"rgba" : L"rgb24"});
  args.push_back(candidate.wstring()); if (progress) progress({static_cast<double>(trial)/trials*.95,"Encoding image"}); ensure(run(engine.ffmpeg,args,cancel));
  if (!strict || fs::file_size(candidate) <= o.target_bytes()) { fs::copy_file(candidate,output,fs::copy_options::overwrite_existing); found = true; low = rank + 1; }
  else { if (!trial) break; high = rank - 1; }
 }
 return found;
}
static std::vector<Artifact> extract_subtitles(const Engine& engine, const Media& media, const Options& options,
 const fs::path& workspace, const fs::path& directory, const std::atomic_bool* cancel) {
 auto o = options; if (o.subtitle_tracks.empty() && !o.mux_external) for (auto& t : media.tracks) if (t.text_subtitle) o.subtitle_tracks.push_back(t.index);
 auto subs = prepare_subtitles(engine,media,o,workspace,cancel,false); if (subs.empty()) throw Error("No text subtitle tracks selected.");
 std::vector<Artifact> artifacts;
 for (size_t i = 0; i < subs.size(); ++i) { auto output = publish_file(subs[i].path,directory,media.path.stem().wstring() + L"_sub" + std::to_wstring(i+1),subs[i].codec == "ass" ? "ass" : "srt"); artifacts.push_back({output,fs::file_size(output)}); } return artifacts;
}
static Media concat_input(const Engine& engine, const Request& request, const std::vector<Media>& media,
 const fs::path& workspace, const std::atomic_bool* cancel, ProgressCallback progress) {
 auto o = request.options; auto difference = concat_incompatibility(media,request.inputs);
 if (!o.normalize_concat && !difference.empty()) throw Error(difference);
 auto [first_w,first_h] = displayed_size(media[0],request.inputs[0].video);
 std::vector<fs::path> parts; double duration_sum = 0; int canvas_w = o.width ? o.width : first_w, canvas_h = o.height ? o.height : first_h;
 canvas_w -= canvas_w % 2; canvas_h -= canvas_h % 2; double fps = o.fps ? o.fps : media[0].track(media[0].video)->fps; if (fps <= 0) fps = 30;
 bool any_audio = !o.mute && std::any_of(media.begin(),media.end(),[](auto& m) { return m.audio >= 0; });
 for (size_t i = 0; i < media.size(); ++i) {
  check_cancel(cancel); auto& m = media[i]; auto& in = request.inputs[i]; double end = in.end > 0 ? std::min(in.end,m.duration) : m.duration, length = end - in.start;
  if (length <= 0) throw Error("Invalid concat clip range."); duration_sum += length;
  auto part = workspace / (L"part" + std::to_wstring(i) + (o.normalize_concat ? L".mkv" : L".mkv")); auto args = base(m.path);
  int video = in.video >= 0 ? in.video : m.video, audio = in.audio == -2 ? m.audio : in.audio;
  if (o.normalize_concat) {
   if (any_audio && audio < 0) add(args,{L"-f",L"lavfi",L"-i",L"anullsrc=r=" + std::to_wstring(o.sample_rate) + L":cl=" + (o.channels == 1 ? L"mono" : L"stereo")});
   std::string tone = m.hdr ? "zscale=t=linear:npl=100,format=gbrpf32le,zscale=p=bt709,tonemap=tonemap=hable:desat=0,zscale=t=bt709:m=bt709:r=tv,format=yuv420p," : "";
   add(args,{L"-map",L"0:" + std::to_wstring(video),L"-vf",wide("trim=start="+num(in.start)+":end="+num(end)+",setpts=PTS-STARTPTS,scale=w='max(1,round(iw*sar))':h=ih,setsar=1,"+tone+"scale="+std::to_string(canvas_w)+":"+std::to_string(canvas_h)+":force_original_aspect_ratio=decrease:force_divisible_by=2,pad="+std::to_string(canvas_w)+":"+std::to_string(canvas_h)+":(ow-iw)/2:(oh-ih)/2,setsar=1,fps="+num(fps)),L"-c:v",L"libx264",L"-crf",L"18",L"-preset",L"fast",L"-pix_fmt",L"yuv420p"});
   if (m.hdr) add(args,{L"-x264-params",L"colorprim=bt709:transfer=bt709:colormatrix=bt709"});
   if (any_audio) {
    add(args,{L"-map",audio >= 0 ? L"0:"+std::to_wstring(audio) : L"1:a:0",L"-af",wide(audio >= 0 ? "atrim=start="+num(in.start)+":end="+num(end)+",asetpts=PTS-STARTPTS,apad" : "atrim=duration="+num(length)),L"-c:a",L"aac",L"-b:a",std::to_wstring(o.audio_kbps)+L"k",L"-ar",std::to_wstring(o.sample_rate),L"-ac",std::to_wstring(o.channels)});
   }
   add(args,{L"-t",number(length)});
  } else {
   if (in.start > 0) add(args,{L"-ss",number(in.start)}); add(args,{L"-t",number(length),L"-map",L"0:"+std::to_wstring(video)});
   if (any_audio && audio >= 0) add(args,{L"-map",L"0:"+std::to_wstring(audio)}); add(args,{L"-c",L"copy",L"-avoid_negative_ts",L"make_zero"});
  }
  add(args,{L"-sn",L"-dn",part.wstring()}); if (progress) progress({.5*static_cast<double>(i)/media.size(),"Preparing concat clip " + std::to_string(i+1)}); ensure(run(engine.ffmpeg,args,cancel)); parts.push_back(part);
 }
 std::string list; for (auto& part : parts) list += "file '" + utf8(part.filename().wstring()) + "'\n";
 auto list_file = workspace / L"concat.txt"; write_text(list_file,list); auto combined = workspace / L"combined.mkv";
 ensure(run(engine.ffmpeg,{L"-v",L"error",L"-nostdin",L"-y",L"-protocol_whitelist",L"file,pipe",L"-f",L"concat",L"-safe",L"1",L"-i",list_file.wstring(),L"-map",L"0",L"-c",L"copy",combined.wstring()},cancel,{},0,workspace));
 auto m = probe(engine,combined,cancel); if (o.normalize_concat && std::abs(m.duration-duration_sum) > std::max(.5, .05*media.size())) throw Error("Concat duration validation failed."); return m;
}
Result process(const Engine& engine, const Request& request, const std::atomic_bool* cancel, ProgressCallback progress) {
 try {
  check_cancel(cancel); auto o = request.options; o.validate(); if (request.inputs.empty()) throw Error("No input files.");
  if (progress) progress({0,"Probing"}); auto media = probe(engine,request.inputs[0].path,cancel);
  if (media.animated) throw Error("Animated images are not supported in this release.");
  if (o.video_track >= 0) if (auto track = media.track(o.video_track); track && track->type == "video") {
   media.video = track->index; media.width = track->width; media.height = track->height;
   auto transfer = track->metadata.value("color_transfer",""); media.hdr = transfer=="smpte2084" || transfer=="arib-std-b67";
  }
  if (media.kind == Kind::Image && o.output_kind==Kind::Audio) throw Error("Image input has no audio stream.");
  if (media.kind == Kind::Image) o.output_kind = Kind::Image; if (media.kind == Kind::Audio) o.output_kind = Kind::Audio;
  if (o.action == Action::Snapshot) o.output_kind = Kind::Image;
  auto directory = o.output_directory.empty() ? media.path.parent_path() / L"Convertii" : fs::absolute(o.output_directory); Workspace workspace(directory);
  if (o.action == Action::ExtractSubtitles) { auto outputs = extract_subtitles(engine,media,o,workspace.path,directory,cancel); return {State::Completed,"Text subtitle extraction completed.",outputs}; }
  if (o.action == Action::Concat) {
   if (request.inputs.size() < 2) throw Error("At least two files are required for concat."); std::vector<Media> all{media}; for (size_t i = 1; i < request.inputs.size(); ++i) all.push_back(probe(engine,request.inputs[i].path,cancel));
   auto difference = concat_incompatibility(all,request.inputs);
   if (o.normalize_concat && !o.allow_sdr && std::any_of(all.begin(),all.end(),[](auto& m){return m.hdr;})) return {State::Confirmation,"HDR input requires confirmation to convert to SDR.",{},Confirmation::Hdr};
   if (!o.normalize_concat && !difference.empty()) return {State::Confirmation,difference,{},Confirmation::Concat};
   if (o.normalize_concat && !o.confirm_concat) return {State::Confirmation,"Confirm normalized re-encoding: one video + at most one audio track; subtitles are omitted.",{},Confirmation::Concat};
   media = concat_input(engine,request,all,workspace.path,cancel,progress); o.action = o.normalize_concat ? (o.target_mode ? Action::Compress : Action::Convert) : Action::Remux;
   o.start = o.end = 0; o.speed = 1; o.video_track = -1; o.audio_tracks.clear(); o.subtitle_tracks.clear(); o.external_subtitle.clear(); o.mux_external = o.burn_external = false; o.burn_subtitle = -1;
   if (!request.options.normalize_concat) { o.height = o.width = 0; o.fps = 0; }
  }
  bool will_encode = o.action != Action::Remux && !o.fast_trim;
  if (media.hdr && will_encode && !o.allow_sdr) return {State::Confirmation,"HDR input requires confirmation to convert to SDR.",{},Confirmation::Hdr};
  if (o.output_kind == Kind::Image && media.alpha && o.image_format == "jpeg" && !o.allow_alpha) return {State::Confirmation,"JPEG cannot preserve transparency. Confirm the background color.",{},Confirmation::Alpha};
  Plan p;
  try { p = plan(media,o); } catch (const Error& e) { if (std::string(e.what()).starts_with("Target is too small")) return {State::Confirmation,e.what(),{},Confirmation::Size}; throw; }
  if (p.copy) validate_copy(media,o,p);
  if (o.output_kind == Kind::Audio && p.audios.size() != 1) throw Error("Select exactly one audio track for audio extraction/conversion.");
  auto extension = o.output_kind == Kind::Image ? (o.image_format == "jpeg" ? "jpg" : o.image_format) : p.extension;
  auto temporary = workspace.path / wide("output." + extension); std::string notes;
  if (o.output_kind == Kind::Image) {
   if (!encode_image(engine,media,o,temporary,workspace.path,cancel,progress)) return {State::Confirmation,"Target cannot be reached at this image quality/size. Change format, dimensions or target.",{},Confirmation::Size};
  } else {
   if (!p.copy && !p.strict && o.video_format != "webm" && o.output_kind != Kind::Audio && o.encoder != "software") {
    auto capabilities = detect_hardware(engine,cancel); std::string wanted = o.encoder == "nvidia" ? "h264_nvenc" : o.encoder == "intel" ? "h264_qsv" : o.encoder == "amd" ? "h264_amf" : "";
    for (auto& c : capabilities) if (c.available && (wanted.empty() || wanted == c.encoder)) { p.video_encoder = c.encoder; break; }
    if (p.video_encoder == "libx264") notes += "Hardware unavailable; used software encoding. ";
   }
   if (p.strict && o.output_kind == Kind::Audio && p.audio_encoder != "flac" && p.audio_encoder != "pcm_s16le") {
    auto budget = static_cast<int>((o.target_bytes() * .95 - 4096) * 8 / p.duration / 1000); if (budget < 16) return {State::Confirmation,"Target is too small for the edited duration.",{},Confirmation::Size}; o.audio_kbps = std::min(o.audio_kbps,budget);
   }
   bool reached = false;
   for (int attempt = 0; attempt < (p.strict ? 3 : 1); ++attempt) {
    try { encode_av(engine,media,o,p,temporary,workspace.path,cancel,progress); }
    catch (const Cancelled&) { throw; }
    catch (const Error&) { if (p.video_encoder != "h264_nvenc" && p.video_encoder != "h264_qsv" && p.video_encoder != "h264_amf") throw; p.video_encoder = "libx264"; notes += "Hardware encoding failed; retried with software encoding. "; encode_av(engine,media,o,p,temporary,workspace.path,cancel,progress); }
    auto bytes = fs::file_size(temporary); if (!p.strict || bytes <= o.target_bytes()) { reached = true; break; }
    if (o.output_kind == Kind::Audio) { if (p.audio_encoder == "flac" || p.audio_encoder == "pcm_s16le") break; o.audio_kbps = static_cast<int>(o.audio_kbps * static_cast<double>(o.target_bytes()) / bytes * .9); if (o.audio_kbps < 16) break; }
    else { p.video_bps = static_cast<int64_t>(p.video_bps * static_cast<double>(o.target_bytes()) / bytes * .93); if (p.video_bps < 20000) break; }
   }
   if (!reached) return {State::Confirmation,"Output exceeds the target after bounded retries. Reduce dimensions/frame rate or increase target.",{},Confirmation::Size};
  }
  check_cancel(cancel); if (progress) progress({.98,"Validating output"}); auto output = probe(engine,temporary,cancel);
  if (!output.bytes || output.kind != o.output_kind) throw Error("Output validation failed.");
  if (o.output_kind != Kind::Image && !p.copy && std::abs(output.duration-p.duration) > std::max(.5,std::min(2.,p.duration*.01))) throw Error("Output duration differs from the edited timeline; incomplete output discarded.");
  if (p.strict && output.bytes > o.target_bytes()) throw Error("Output exceeds the size limit.");
  check_cancel(cancel); auto filename = publish_file(temporary,directory,request.inputs[0].path.stem().wstring(),extension);
  notes += "Only selected streams were exported; chapters/attachments were omitted."; if (progress) progress({1,"Completed"}); return {State::Completed,notes,{{filename,output.bytes}}};
 } catch (const Cancelled&) { return {State::Cancelled,"Cancelled."}; }
 catch (const std::exception& e) { auto message = std::string(e.what()); if (message == "Subtitle style conversion needs confirmation.") return {State::Confirmation,message,{},Confirmation::SubtitleStyles}; return {State::Failed,message}; }
}
void clean_preview_cache(uint64_t limit) {
 auto root = preview_cache_root(); if (!fs::exists(root)) return;
 std::vector<fs::directory_entry> files; uint64_t total = 0;
 for (auto& e : fs::directory_iterator(root)) if (e.is_regular_file()) { files.push_back(e); total += e.file_size(); }
 std::sort(files.begin(),files.end(),[](auto& a,auto& b) { return a.last_write_time() < b.last_write_time(); });
 for (auto& e : files) { std::error_code error; bool old = fs::file_time_type::clock::now()-e.last_write_time() > std::chrono::hours(24); if (total > limit || old) { auto size = e.file_size(); if (fs::remove(e.path(),error)) total -= size; } }
 // Unique workspaces from interrupted previews cannot contain user files.
 for (auto& e : fs::directory_iterator(root)) if (e.is_directory() && e.path().filename().wstring().starts_with(L".convertii-") && fs::file_time_type::clock::now()-e.last_write_time() > std::chrono::hours(24)) { std::error_code error; fs::remove_all(e.path(),error); }
}
PreviewSpec render_preview(const Engine& engine, const Input& input, const Options& options, double position, bool effects, const std::atomic_bool* cancel) {
 auto media = probe(engine,input.path,cancel); if (media.kind != Kind::Video) throw Error("Preview requires a video file."); clean_preview_cache();
 auto o = effects ? options : Options{}; double start = std::clamp(position,0.,std::max(0.,media.duration-.05));
 if (effects) start = std::max(start,options.start); double end = std::min(media.duration,start+(effects ? 10*o.speed : 30)); if (effects && options.end > 0) end = std::min(end,options.end);
 if (end <= start) throw Error("Preview position is outside the selected range.");
 o.action = Action::Convert; o.target_mode = false; o.output_kind = Kind::Video; o.start = start; o.end = end; o.height = std::min(720,o.height > 0 ? o.height : 720); o.width = std::min(1280,o.width > 0 ? o.width : 1280); o.video_format = "mp4"; o.encoder = "software"; o.video_kbps = 0; o.fast_trim = false; o.quality = 1; o.output_directory = preview_cache_root();
 auto result = process(engine,{{input},o},cancel); if (result.state != State::Completed) throw Error(result.message);
 clean_preview_cache(); return {result.outputs[0].path,start,o.speed,effects};
}
fs::path preview_cache_root() { wchar_t value[32768]; if (GetEnvironmentVariableW(L"CONVERTII_CACHE_DIR",value,32768)) return value; return temp_root()/L"Preview"; }
}
