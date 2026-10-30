#include <convertii/core.h>
#include <iostream>
#include <fstream>
#include <cmath>

using namespace cvt;
int main() {
 try {
  Engine engine = Engine::discover(); auto root = fs::absolute(fs::path(L"artifacts")/wide("native-tests-"+unique_id())); fs::create_directories(root); SetEnvironmentVariableW(L"CONVERTII_CACHE_DIR",(root/L"cache").c_str()); unsigned count=0;
  auto check = [&](bool condition,const std::string& name) { if (!condition) throw Error("FAIL: "+name); ++count; std::cout << "PASS: " << name << '\n'; };
  auto generate = [&](const wchar_t* name,std::vector<std::wstring> args) { args.insert(args.begin(),{L"-v",L"error",L"-y"}); args.push_back((root/name).wstring()); auto result=run(engine.ffmpeg,args); if (result.code) throw Error(result.error); };
  Options options; options.target_mb=.12; options.encoder="software"; options.output_directory=root/L"output";
  auto perform = [&](const wchar_t* name,Options o) { return process(engine,{{{root/name}},o}); };
  generate(L"中文 clip.mp4",{L"-f",L"lavfi",L"-i",L"testsrc2=size=320x240:rate=24:duration=3",L"-f",L"lavfi",L"-i",L"sine=frequency=440:duration=3",L"-c:v",L"libx264",L"-c:a",L"aac"});
  std::cout<<std::unitbuf;
  generate(L"anamorphic.mp4",{L"-f",L"lavfi",L"-i",L"testsrc2=size=240x180:rate=10:duration=1",L"-vf",L"setsar=4/3",L"-c:v",L"libx264"});
  auto square_options=options; square_options.action=Action::Convert;
  auto square=perform(L"anamorphic.mp4",square_options); check(square.state==State::Completed,"Anamorphic conversion: "+square.message);
  auto square_info=probe(engine,square.outputs[0].path);
  check(square_info.width==320 && square_info.height==180 && square_info.track(square_info.video)->metadata.value("sample_aspect_ratio","")=="1:1","Anamorphic display aspect preserved with square pixels");
  auto hash=sha256(root/L"中文 clip.mp4"); auto media=probe(engine,root/L"中文 clip.mp4"); check(media.kind==Kind::Video && media.audio>=0 && media.duration>=3,"Probe video/audio");
  auto planned=plan(media,options); check(planned.two_pass && planned.video_encoder=="libx264","Strict software two-pass");
  auto result=perform(L"中文 clip.mp4",options); check(result.state==State::Completed && result.outputs[0].bytes<=options.target_bytes(),"Video byte upper bound: "+result.message);
  check(std::abs(probe(engine,result.outputs[0].path).duration-media.duration)<.2,"Full output duration");
  auto duplicate=perform(L"中文 clip.mp4",options); check(duplicate.state==State::Completed && duplicate.outputs[0].path!=result.outputs[0].path,"Never overwrite existing output"); check(sha256(root/L"中文 clip.mp4")==hash,"Original hash unchanged");
  auto impossible=options; impossible.target_mb=.001; check(perform(L"中文 clip.mp4",impossible).state==State::Confirmation,"Impossible target requires confirmation");
  generate(L"silent.mp4",{L"-f",L"lavfi",L"-i",L"testsrc2=size=120x200:duration=1:rate=15",L"-an",L"-c:v",L"libx264"}); check(perform(L"silent.mp4",options).state==State::Completed,"Portrait/no audio");
  for (auto format:{"mkv","webm"}) { auto o=options; o.action=Action::Convert; o.video_format=format; auto r=perform(L"silent.mp4",o); check(r.state==State::Completed,std::string("Video conversion ")+format+": "+r.message); }
  auto vp9=options; vp9.video_format="webm"; check(perform(L"silent.mp4",vp9).state==State::Completed,"VP9 two-pass");
  generate(L"multi.mkv",{L"-f",L"lavfi",L"-i",L"testsrc2=size=128x96:duration=1:rate=10",L"-f",L"lavfi",L"-i",L"color=size=128x96:duration=3:rate=10",L"-f",L"lavfi",L"-i",L"sine=duration=1",L"-f",L"lavfi",L"-i",L"sine=duration=3",L"-map",L"0:v",L"-map",L"1:v",L"-map",L"2:a",L"-map",L"3:a",L"-c:v",L"libx264",L"-c:a",L"aac",L"-disposition:v:0",L"default",L"-disposition:v:1",L"0",L"-disposition:a:0",L"default",L"-disposition:a:1",L"0"});
  auto multi=perform(L"multi.mkv",options); check(multi.state==State::Completed,"Default tracks with longer omitted streams: "+multi.message); auto multi_info=probe(engine,multi.outputs[0].path); check(multi_info.tracks.size()==2 && multi_info.duration<1.5,"Only default tracks exported");
  auto selected=options; selected.video_track=1; selected.audio_tracks={3}; auto selected_result=perform(L"multi.mkv",selected); check(selected_result.state==State::Completed && probe(engine,selected_result.outputs[0].path).duration>2.8,"Explicit non-default track duration: "+selected_result.message);
  generate(L"alpha.png",{L"-f",L"lavfi",L"-i",L"color=c=red@0.3:s=128x96,format=rgba",L"-frames:v",L"1"}); auto image=probe(engine,root/L"alpha.png"); check(image.kind==Kind::Image && image.alpha,"Image alpha detection");
  for (auto format:{"webp","png"}) { auto o=options; o.image_format=format; o.target_mb=.01; auto r=perform(L"alpha.png",o); check(r.state==State::Completed && r.outputs[0].bytes<=10000,std::string("Strict transparent ")+format+": "+r.message); }
  auto jpeg=options; jpeg.image_format="jpeg"; check(perform(L"alpha.png",jpeg).confirmation==Confirmation::Alpha,"JPEG needs alpha consent"); jpeg.allow_alpha=true; check(perform(L"alpha.png",jpeg).state==State::Completed,"JPEG background fill");
  auto tiny=options; tiny.image_format="png"; tiny.target_mb=.00001; check(perform(L"alpha.png",tiny).state==State::Confirmation,"Impossible PNG size");
  generate(L"animation.gif",{L"-f",L"lavfi",L"-i",L"testsrc2=size=64x64:duration=1:rate=3"}); check(perform(L"animation.gif",options).state==State::Failed,"Animation rejected");
  generate(L"hdr.mp4",{L"-f",L"lavfi",L"-i",L"testsrc2=size=128x96:duration=1:rate=10",L"-c:v",L"libx264",L"-x264-params",L"colorprim=bt2020:transfer=smpte2084:colormatrix=bt2020nc"}); check(perform(L"hdr.mp4",options).confirmation==Confirmation::Hdr,"HDR requires consent"); auto hdr=options; hdr.allow_sdr=true; auto sdr=perform(L"hdr.mp4",hdr); check(sdr.state==State::Completed && !probe(engine,sdr.outputs[0].path).hdr,"HDR tone mapping: "+sdr.message);
  generate(L"oriented.jpg",{L"-f",L"lavfi",L"-i",L"testsrc2=size=128x96",L"-frames:v",L"1"}); auto image_data=read_text(root/L"oriented.jpg");
  const unsigned char exif[]={0xff,0xe1,0,0x22,0x45,0x78,0x69,0x66,0,0,0x49,0x49,0x2a,0,8,0,0,0,1,0,0x12,1,3,0,1,0,0,0,6,0,0,0,0,0,0,0}; image_data.insert(2,reinterpret_cast<const char*>(exif),sizeof(exif)); write_text(root/L"oriented.jpg",image_data);
  auto oriented_options=options; oriented_options.image_format="png"; auto oriented=perform(L"oriented.jpg",oriented_options); check(oriented.state==State::Completed,"EXIF JPEG conversion"); auto oriented_info=probe(engine,oriented.outputs[0].path); check(oriented_info.width==96 && oriented_info.height==128,"EXIF orientation applied");
  SetEnvironmentVariableW(L"CONVERTII_TEST_FFMPEG",engine.ffmpeg.c_str()); auto fake_engine=engine; fake_engine.ffmpeg=executable_dir()/L"HardwareStub.exe"; auto hardware_options=options; hardware_options.target_mode=false; hardware_options.encoder="nvidia"; auto fallback=process(fake_engine,{{{root/L"中文 clip.mp4"}},hardware_options}); check(fallback.state==State::Completed && fallback.message.find("Hardware encoding failed")!=std::string::npos,"Hardware failure falls back to actual software encoding: "+fallback.message);
  std::atomic_bool cancel{true}; check(process(engine,{{{root/L"silent.mp4"}},options},&cancel).state==State::Cancelled,"Pre-cancelled"); cancel=false;
  auto cancelled=process(engine,{{{root/L"中文 clip.mp4"}},options},&cancel,[&](auto& p) { if (p.message.starts_with("Encoding")) cancel=true; }); check(cancelled.state==State::Cancelled,"Cancellation during encoding");
  write_text(root/L"broken.mp4","broken"); check(perform(L"broken.mp4",options).state==State::Failed,"Corrupt input"); write_text(root/L"blocked","file"); auto blocked=options; blocked.output_directory=root/L"blocked"/L"folder"; check(perform(L"silent.mp4",blocked).state==State::Failed,"Blocked output directory");
  auto audio=options; audio.action=Action::Convert; audio.output_kind=Kind::Audio;
  for (auto format:{"mp3","m4a","flac","wav"}) { audio.audio_format=format; auto r=perform(L"中文 clip.mp4",audio); check(r.state==State::Completed && probe(engine,r.outputs[0].path).kind==Kind::Audio,std::string("Audio extraction ")+format+": "+r.message); if (std::string(format)=="wav") { auto r2=process(engine,{{{r.outputs[0].path}},audio}); check(r2.state==State::Completed,"Standalone audio input"); } }
  auto edited=options; edited.action=Action::Convert; edited.start=.5; edited.end=2.5; edited.speed=2; edited.crop_w=160; edited.crop_h=120; edited.rotation=90; edited.gray=true; edited.volume_db=-6; auto edit=perform(L"中文 clip.mp4",edited); check(edit.state==State::Completed,"Combined trim/crop/rotate/speed/audio: "+edit.message); auto edited_info=probe(engine,edit.outputs[0].path); check(edited_info.width==120 && edited_info.height==160 && std::abs(edited_info.duration-1)<.25,"Edited geometry and duration");
  auto strict_edit=edited; strict_edit.action=Action::Compress; auto edited_cap=perform(L"中文 clip.mp4",strict_edit); check(edited_cap.state==State::Completed && edited_cap.outputs[0].bytes<=strict_edit.target_bytes(),"Edited duration used for byte budget");
  auto remux=options; remux.action=Action::Remux; remux.video_format="mkv"; auto mux=perform(L"中文 clip.mp4",remux); check(mux.state==State::Completed && probe(engine,mux.outputs[0].path).track(0)->codec=="h264","Remux preserves codec"); remux.gray=true; check(perform(L"中文 clip.mp4",remux).state==State::Failed,"Stream copy rejects filters");
  remux.gray=false; remux.fast_trim=true; remux.start=.5; remux.end=2; check(perform(L"中文 clip.mp4",remux).state==State::Completed,"Fast keyframe trim");
  auto snap=options; snap.action=Action::Snapshot; snap.snapshot_time=1; snap.image_format="png"; auto shot=perform(L"中文 clip.mp4",snap); check(shot.state==State::Completed && probe(engine,shot.outputs[0].path).kind==Kind::Image,"Snapshot at source time: "+shot.message);
  std::string subtitle="1\n00:00:00,000 --> 00:00:02,000\nHello\n\n2\n00:00:02,000 --> 00:00:03,000\nWorld\n\n"; auto shifted=transform_subtitles(subtitle,false,1,3,2,0); check(shifted.find("00:00:00,000 --> 00:00:00,500")!=std::string::npos,"Subtitle trim/speed timeline"); write_text(root/L"中文 subtitle.srt",subtitle);
  auto sub=options; sub.action=Action::Convert; sub.external_subtitle=root/L"中文 subtitle.srt"; sub.burn_external=true; sub.start=1; sub.end=3; sub.speed=2; auto burn=perform(L"中文 clip.mp4",sub); check(burn.state==State::Completed,"Subtitle burn + trim + speed: "+burn.message);
  sub.burn_external=false; sub.mux_external=true; sub.video_format="mkv"; auto soft=perform(L"中文 clip.mp4",sub); check(soft.state==State::Completed,"Soft subtitle mux: "+soft.message);
  auto extract=options; extract.action=Action::ExtractSubtitles; auto extracted=process(engine,{{{soft.outputs[0].path}},extract}); check(extracted.state==State::Completed && !extracted.outputs.empty(),"Subtitle extraction: "+extracted.message);
  Request concat{{{root/L"silent.mp4"},{root/L"中文 clip.mp4"}},options}; concat.options.action=Action::Concat; check(process(engine,concat).confirmation==Confirmation::Concat,"Fast concat detects mismatch"); concat.options.normalize_concat=true; concat.options.confirm_concat=true; auto normalized=process(engine,concat); check(normalized.state==State::Completed,"Normalize concat and add missing audio: "+normalized.message); check(std::abs(probe(engine,normalized.outputs[0].path).duration-4)<.5,"Concat duration sum");
  auto preview_options=options; auto preview=render_preview(engine,{root/L"中文 clip.mp4"},preview_options,1,true,nullptr); check(fs::exists(preview.file) && preview.source_start==1 && probe(engine,preview.file).height<=720,"Effect preview shared pipeline");
  generate(L"slow-preview.mp4",{L"-f",L"lavfi",L"-i",L"testsrc2=size=128x96:duration=4:rate=10",L"-c:v",L"libx264"});
  auto slow_options=options; slow_options.speed=.25; slow_options.width=64;
  auto slow_sample=render_preview(engine,{root/L"slow-preview.mp4"},slow_options,0,true,nullptr); auto slow_info=probe(engine,slow_sample.file);
  check(slow_info.width==64 && slow_info.height==48 && slow_info.duration<=10.1 && slow_sample.speed==.25,"Preview respects edited width and ten-second output cap at slow speed");
  cancel=true; bool preview_cancelled=false; try{render_preview(engine,{root/L"中文 clip.mp4"},options,0,true,&cancel);}catch(const Cancelled&){preview_cancelled=true;} check(preview_cancelled,"Preview cancellation"); clean_preview_cache(0); check(fs::is_empty(preview_cache_root()),"Preview cache byte limit enforced");
  auto settings_root=root/L"settings"; fs::create_directories(settings_root); write_text(settings_root/L"settings.json",R"({"Language":"zh-CN","Options":{"TargetMegabytes":5,"Encoder":1}})"); auto settings=load_settings(settings_root); check(settings.chinese && settings.options.target_mb==5 && fs::exists(settings_root/L"settings.v1.bak"),"Legacy settings migration and backup"); check(load_settings(settings_root).options.encoder=="software","Native settings round trip");
  check(quote(L"x\\\" y\\")==L"\"x\\\\\\\" y\\\\\"","Windows quoting");
  bool clean=true; for (auto& e:fs::recursive_directory_iterator(root)) if(e.is_directory() && e.path().filename().wstring().starts_with(L".convertii-")) clean=false; check(clean,"All temporary workspaces cleaned");
  std::cout<<count<<" native media checks passed. Fixtures: "<<utf8(root.wstring())<<'\n'; return 0;
 } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
