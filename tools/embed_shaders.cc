#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

bool ReadBytes(const fs::path& path, std::vector<uint8_t>* out) {
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in.is_open()) return false;
  const std::streamsize size = in.tellg();
  if (size < 0) return false;
  out->resize(static_cast<size_t>(size));
  in.seekg(0, std::ios::beg);
  return size == 0 ||
         static_cast<bool>(in.read(reinterpret_cast<char*>(out->data()), size));
}

std::string MakeIdent(const std::string& name) {
  std::string out = name;
  for (char& ch : out) {
    if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
          (ch >= '0' && ch <= '9'))) {
      ch = '_';
    }
  }
  return out;
}

std::string TrimLeft(const std::string& s) {
  size_t i = 0;
  while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
  return s.substr(i);
}

bool EndsWith(const std::string& s, const std::string& suffix) {
  return s.size() >= suffix.size() &&
         s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Strips ARB-only layout/extension qualifiers so bare OpenGL 3.3 Core drivers
// can still compile the fallback path cleanly when SPIR-V is unavailable.
std::string BuildGlsl330Fallback(const std::vector<uint8_t>& raw,
                                 bool is_vertex) {
  std::string src(raw.begin(), raw.end());
  std::istringstream iss(src);
  std::ostringstream oss;
  std::string line;
  while (std::getline(iss, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const std::string trimmed = TrimLeft(line);
    if (trimmed.rfind("#extension ", 0) == 0) {
      continue;
    }
    if (trimmed.rfind("layout(", 0) == 0) {
      const size_t close = trimmed.find(')');
      if (close != std::string::npos) {
        const std::string rest = TrimLeft(trimmed.substr(close + 1));
        const bool keep_vert_attrib = is_vertex && rest.rfind("in ", 0) == 0;
        if (!keep_vert_attrib) {
          oss << rest << '\n';
          continue;
        }
      }
    }
    oss << line << '\n';
  }
  return oss.str();
}

void WriteHexArray(std::ofstream& out, const std::string& var_name,
                   const uint8_t* data, size_t size, bool add_null) {
  out << "constexpr uint8_t " << var_name << "[] = {";
  const size_t total = size + (add_null ? 1 : 0);
  for (size_t i = 0; i < total; ++i) {
    if (i % 16 == 0) out << "\n ";
    const uint8_t byte = (i < size) ? data[i] : 0;
    out << " 0x" << std::hex << std::setw(2) << std::setfill('0')
        << static_cast<unsigned int>(byte) << ",";
  }
  out << std::dec << "\n};\n";
}

}  // namespace

int main(int argc, char* argv[]) {
  if (argc < 5) {
    std::cerr << "Usage: embed_shaders <out_cc> <shader_dir> <spv_dir> "
                 "<shader1> [shader2...]\n";
    return 1;
  }

  const fs::path out_cc(argv[1]);
  const fs::path shader_dir(argv[2]);
  const fs::path spv_dir(argv[3]);

  fs::create_directories(out_cc.parent_path());
  std::ofstream out(out_cc, std::ios::binary);
  if (!out.is_open()) {
    std::cerr << "Failed to open output file: " << out_cc << "\n";
    return 1;
  }

  out << "// Auto-generated at build time by embed_shaders. Do not edit.\n"
      << "#include \"platform/pc/precompiled_shaders.h\"\n\n"
      << "namespace minicraft::platform::pc {\n"
      << "namespace {\n\n";

  struct Entry {
    std::string name;
    std::string ident;
    size_t spv_size = 0;
  };
  std::vector<Entry> entries;

  for (int i = 4; i < argc; ++i) {
    const std::string name = argv[i];
    const std::string ident = MakeIdent(name);

    std::vector<uint8_t> spv;
    if (!ReadBytes(spv_dir / (name + ".spv"), &spv) || spv.empty()) {
      std::cerr << "Failed to read precompiled SPIR-V: "
                << (spv_dir / (name + ".spv")) << "\n";
      return 1;
    }

    std::vector<uint8_t> glsl_raw;
    if (!ReadBytes(shader_dir / name, &glsl_raw) || glsl_raw.empty()) {
      std::cerr << "Failed to read shader source: " << (shader_dir / name)
                << "\n";
      return 1;
    }

    const std::string glsl =
        BuildGlsl330Fallback(glsl_raw, EndsWith(name, ".vert"));
    WriteHexArray(out, "kSpv_" + ident, spv.data(), spv.size(), false);
    WriteHexArray(out, "kGlsl_" + ident,
                  reinterpret_cast<const uint8_t*>(glsl.data()), glsl.size(),
                  true);
    out << "\n";

    entries.push_back({name, ident, spv.size()});
  }

  out << "const PrecompiledShader kAllShaders[] = {\n";
  for (const Entry& e : entries) {
    out << "    {\"" << e.name << "\", kSpv_" << e.ident << ", " << e.spv_size
        << ", reinterpret_cast<const char*>(kGlsl_" << e.ident << ")},\n";
  }
  out << "};\n\n"
      << "}  // namespace\n\n"
      << "const PrecompiledShader* FindPrecompiledShader(std::string_view name) {\n"
      << "  for (const auto& s : kAllShaders) {\n"
      << "    if (s.name == name) return &s;\n"
      << "  }\n"
      << "  return nullptr;\n"
      << "}\n\n"
      << "const PrecompiledShader* AllPrecompiledShaders(size_t* out_count) {\n"
      << "  if (out_count) *out_count = sizeof(kAllShaders) / sizeof(kAllShaders[0]);\n"
      << "  return kAllShaders;\n"
      << "}\n\n"
      << "}  // namespace minicraft::platform::pc\n";

  return 0;
}
