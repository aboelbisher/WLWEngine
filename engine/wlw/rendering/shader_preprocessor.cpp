#include "wlw/rendering/shader_preprocessor.h"

#include <algorithm>
#include <regex>
#include <set>

namespace wlw::rendering {

namespace {

enum class IncludeLine { None, Ok, Malformed };

// `#include "name"` (spaces allowed around the parts, a // comment after it).
IncludeLine ParseInclude(std::string_view line, std::string& name) {
  size_t i = line.find_first_not_of(" \t");
  if (i == std::string_view::npos || line[i] != '#') return IncludeLine::None;
  i = line.find_first_not_of(" \t", i + 1);
  constexpr std::string_view kInclude = "include";
  if (i == std::string_view::npos || line.substr(i, kInclude.size()) != kInclude) return IncludeLine::None;
  i = line.find_first_not_of(" \t", i + kInclude.size());
  if (i == std::string_view::npos || line[i] != '"') return IncludeLine::Malformed;
  const size_t close = line.find('"', i + 1);
  if (close == std::string_view::npos || close == i + 1) return IncludeLine::Malformed;
  const size_t rest = line.find_first_not_of(" \t\r", close + 1);
  if (rest != std::string_view::npos && line.substr(rest, 2) != "//") return IncludeLine::Malformed;
  name.assign(line.substr(i + 1, close - i - 1));
  return IncludeLine::Ok;
}

struct Expander {
  const ShaderFileLookup& lookup;
  std::vector<std::string> files;      // source-string number -> name
  std::vector<std::string> stack;      // files being expanded (for cycles)
  std::set<std::string, std::less<>> included;
  std::string out;
  std::string error;

  // Includes `name` here (once), as source string number files.size(). False on error.
  bool Include(std::string_view name, std::string_view from, int from_line) {
    if (std::find(stack.begin(), stack.end(), name) != stack.end()) {
      error = "include cycle: ";
      for (const std::string& file : stack) error += file + " -> ";
      error += std::string(name);
      return false;
    }
    if (included.count(name)) return true;  // once is enough
    const std::string* text = lookup(name);
    if (text == nullptr) {
      error = std::string(from) + ":" + std::to_string(from_line) + ": #include \"" + std::string(name) +
              "\": no such shader file";
      return false;
    }
    included.emplace(name);
    const int id = static_cast<int>(files.size());
    files.emplace_back(name);
    out += "#line 1 " + std::to_string(id) + "\n";
    return Expand(name, *text, id);
  }

  // Copies `text` (source string `id`) line by line, expanding its #includes.
  bool Expand(std::string_view name, std::string_view text, int id) {
    stack.emplace_back(name);
    int line_number = 0;
    size_t pos = 0;
    while (pos < text.size()) {
      const size_t end = text.find('\n', pos);
      const std::string_view line = text.substr(pos, end == std::string_view::npos ? std::string_view::npos : end - pos);
      ++line_number;
      std::string include;
      switch (ParseInclude(line, include)) {
        case IncludeLine::None:
          out.append(line);
          out += '\n';
          break;
        case IncludeLine::Malformed:
          error = std::string(name) + ":" + std::to_string(line_number) + ": malformed #include (use #include \"file\")";
          return false;
        case IncludeLine::Ok: {
          const size_t before = out.size();
          if (!Include(include, name, line_number)) return false;
          if (out.size() == before) {
            out += '\n';  // already included: an empty line keeps the numbering
          } else {
            out += "#line " + std::to_string(line_number + 1) + " " + std::to_string(id) + "\n";
          }
          break;
        }
      }
      if (end == std::string_view::npos) break;
      pos = end + 1;
    }
    stack.pop_back();
    return true;
  }
};

}  // namespace

PreprocessedShader PreprocessShader(std::string_view name, std::string_view source,
                                    const std::vector<std::string>& defines, const std::vector<std::string>& prelude,
                                    const ShaderFileLookup& lookup) {
  Expander expander{lookup};
  expander.files.emplace_back(name);  // source string 0: the main text
  for (const std::string& define : defines) expander.out += "#define " + define + "\n";
  PreprocessedShader result;
  for (const std::string& file : prelude) {
    if (!expander.Include(file, name, 0)) {
      result.error = expander.error;
      return result;
    }
  }
  expander.out += "#line 1 0\n";
  if (!expander.Expand(name, source, 0)) {
    result.error = expander.error;
    return result;
  }
  result.ok = true;
  result.code = std::move(expander.out);
  result.files = std::move(expander.files);
  return result;
}

std::string MapShaderLog(std::string_view log, const std::vector<std::string>& files) {
  // "<file>:<line>:" (GLES drivers) and "<file>(<line>)" (desktop drivers).
  static const std::regex kPosition(R"((\d+)(?::(\d+):|\((\d+)\)))");
  const std::string text(log);
  std::string out;
  size_t copied = 0;
  for (auto it = std::sregex_iterator(text.begin(), text.end(), kPosition); it != std::sregex_iterator(); ++it) {
    const std::smatch& match = *it;
    const size_t file = std::stoul(match[1].str());
    if (file >= files.size()) continue;
    const std::string line = match[2].matched ? match[2].str() : match[3].str();
    out.append(text, copied, static_cast<size_t>(match.position()) - copied);
    out += files[file] + ":" + line + (match[2].matched ? ":" : "");
    copied = static_cast<size_t>(match.position() + match.length());
  }
  out.append(text, copied, std::string::npos);
  return out;
}

}  // namespace wlw::rendering
