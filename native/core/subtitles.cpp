#include <convertii/core.h>
#include <sstream>
#include <regex>
#include <iomanip>
#include <cmath>

namespace cvt {
static std::string stamp(double seconds, bool ass) {
 auto ticks = static_cast<int64_t>(std::llround(std::max(0., seconds) * (ass ? 100 : 1000)));
 auto unit = ass ? 100 : 1000; std::ostringstream out; out << std::setfill('0');
 out << (ass ? "" : "") << std::setw(ass ? 1 : 2) << ticks / unit / 3600 << ':' << std::setw(2) << ticks / unit / 60 % 60 << ':' << std::setw(2) << ticks / unit % 60 << (ass ? '.' : ',') << std::setw(ass ? 2 : 3) << ticks % unit; return out.str();
}
static double parse_stamp(std::string text) { std::replace(text.begin(), text.end(), ',', '.'); return time_value(text); }
std::string transform_subtitles(const std::string& text, bool ass, double start, double end, double speed, double offset) {
 if (speed < .25 || speed > 4 || end <= start) throw Error("Invalid subtitle timeline.");
 auto transform = [&](double begin, double finish) -> std::optional<std::pair<double,double>> {
  begin = std::max(start, begin + offset); finish = std::min(end, finish + offset); if (finish <= begin) return {};
  return std::pair{(begin - start) / speed, (finish - start) / speed};
 };
 std::istringstream input(text); std::ostringstream output; std::string line;
 if (ass) {
  int start_field = 1, end_field = 2, field_count = 10; bool events = false;
  while (std::getline(input, line)) {
   if (!line.empty() && line.back() == '\r') line.pop_back();
   if (line.starts_with("[")) events = line == "[Events]";
   if (events && line.starts_with("Format:")) {
    std::istringstream format(line.substr(7)); std::string field; int index = 0;
    while (std::getline(format, field, ',')) { field.erase(0, field.find_first_not_of(" \t")); if (field == "Start") start_field = index; if (field == "End") end_field = index; ++index; } field_count = index;
   }
   if (events && (line.starts_with("Dialogue:") || line.starts_with("Comment:"))) {
    auto colon = line.find(':'); std::string prefix = line.substr(0, colon + 1), remaining = line.substr(colon + 1); std::vector<std::string> fields;
    size_t position = 0;
    for (int i = 0; i < field_count - 1; ++i) { auto comma = remaining.find(',', position); if (comma == std::string::npos) break; fields.push_back(remaining.substr(position, comma - position)); position = comma + 1; } fields.push_back(remaining.substr(position));
    if (static_cast<int>(fields.size()) != field_count || start_field >= field_count || end_field >= field_count) throw Error("Unsupported ASS event format.");
    auto times = transform(parse_stamp(fields[start_field]), parse_stamp(fields[end_field])); if (!times) continue;
    fields[start_field] = stamp(times->first, true); fields[end_field] = stamp(times->second, true); output << prefix;
    for (size_t i = 0; i < fields.size(); ++i) output << (i ? "," : "") << fields[i]; output << '\n';
   } else output << line << '\n';
  }
 } else {
  std::regex timing(R"(^\s*([0-9]+:[0-9]+:[0-9]+[,.][0-9]+)\s*-->\s*([0-9]+:[0-9]+:[0-9]+[,.][0-9]+)(.*)$)");
  unsigned index = 0;
  while (std::getline(input, line)) {
   if (!line.empty() && line.back() == '\r') line.pop_back(); std::smatch match;
   if (!std::regex_match(line, match, timing)) continue;
   auto times = transform(parse_stamp(match[1]), parse_stamp(match[2])); std::string body, content;
   while (std::getline(input, content)) { if (!content.empty() && content.back() == '\r') content.pop_back(); if (content.empty()) break; body += content + '\n'; }
   if (times) output << ++index << '\n' << stamp(times->first, false) << " --> " << stamp(times->second, false) << match[3].str() << '\n' << body << '\n';
  }
 }
 return output.str();
}
}
