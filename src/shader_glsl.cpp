// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

#include "shader_glsl.hpp"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <vector>

namespace gh {

namespace {

constexpr const char* kPrelude = R"GLSL(#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
uniform vec4 colDiffuse;
out vec4 gh_FragColor;
#define gh_sample texture
#define gh_sampleLod textureLod
)GLSL";

// OpenGL ES 2 (Android): GLSL ES 1.0, like the shaders themselves, with
// fwidth/dFdx/dFdy where the device has them (nearly all do).
constexpr const char* kPreludeEs = R"GLSL(#version 100
#extension GL_OES_standard_derivatives : enable
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
varying vec2 fragTexCoord;
varying vec4 fragColor;
uniform sampler2D texture0;
uniform vec4 colDiffuse;
#define gh_FragColor gl_FragColor
#define gh_sample texture2D
#define gh_sampleLod(s, uv, lod) texture2D(s, uv)
)GLSL";

// OpenGL 2.1 (older graphics cards): GLSL 1.20, which is also what OpenFL
// compiles these shaders as on desktops. It has no precision qualifiers.
// The DirectX 10 cards of those years (GeForce 8, Radeon HD 2000 on) add
// unsigned integers and bit operations through GL_EXT_gpu_shader4, which
// some shaders written for GLSL 1.30 use (uint, ^, &, <<).
constexpr const char* kPreludeGl120 = R"GLSL(#version 120
#extension GL_EXT_gpu_shader4 : enable
#ifdef GL_EXT_gpu_shader4
#define uint unsigned int
#endif
varying vec2 fragTexCoord;
varying vec4 fragColor;
uniform sampler2D texture0;
uniform vec4 colDiffuse;
#define gh_FragColor gl_FragColor
#define gh_sample texture2D
#define gh_sampleLod(s, uv, lod) texture2D(s, uv)
#define highp
#define mediump
#define lowp
)GLSL";

constexpr const char* kCommon = R"GLSL(uniform vec2 openfl_TextureSize;
uniform float gh_flipY;
uniform float gh_premul;
uniform bool hasTransform;
uniform bool hasColorTransform;
uniform bool openfl_HasColorTransform;
#define bitmap texture0
#define openfl_TextureCoordv vec2(fragTexCoord.x, mix(fragTexCoord.y, 1.0 - fragTexCoord.y, gh_flipY))
#define openfl_Alphav (fragColor.a)
#define openfl_ColorMultiplierv vec4(fragColor.rgb, 1.0)
#define openfl_ColorOffsetv vec4(0.0)
vec4 gh_tex(sampler2D s, vec2 uv) { vec4 c = gh_sample(s, uv); return vec4(c.rgb * c.a, c.a); }
vec4 gh_tex(sampler2D s, vec2 uv, float bias) { vec4 c = gh_sample(s, uv, bias); return vec4(c.rgb * c.a, c.a); }
vec4 gh_texLod(sampler2D s, vec2 uv, float lod) { vec4 c = gh_sampleLod(s, uv, lod); return vec4(c.rgb * c.a, c.a); }
vec4 gh_texBitmap(vec2 uv) {
    vec4 c = gh_sample(texture0, vec2(uv.x, mix(uv.y, 1.0 - uv.y, gh_flipY)));
    return gh_premul > 0.5 ? vec4(c.rgb * c.a, c.a) : c;
}
vec4 gh_texBitmap(vec2 uv, float bias) {
    vec4 c = gh_sample(texture0, vec2(uv.x, mix(uv.y, 1.0 - uv.y, gh_flipY)), bias);
    return gh_premul > 0.5 ? vec4(c.rgb * c.a, c.a) : c;
}
vec4 flixel_texture2D(sampler2D s, vec2 uv) {
    vec4 color = gh_texBitmap(uv);
    if (color.a == 0.0) return vec4(0.0);
    color = vec4(color.rgb / color.a, color.a) * vec4(fragColor.rgb, 1.0);
    return vec4(color.rgb * color.a * fragColor.a, color.a * fragColor.a);
}
#line 1
)GLSL";

bool identChar(char c) { return std::isalnum((unsigned char)c) || c == '_'; }

// inverse() of GLSL 1.40, for GLSL 1.20 and GLSL ES 1.0 (the cofactor formulas).
constexpr const char* kInverse = R"GLSL(mat2 inverse(mat2 m) {
    return mat2(m[1][1], -m[0][1], -m[1][0], m[0][0]) / (m[0][0] * m[1][1] - m[1][0] * m[0][1]);
}
mat3 inverse(mat3 m) {
    float a00 = m[0][0], a01 = m[0][1], a02 = m[0][2];
    float a10 = m[1][0], a11 = m[1][1], a12 = m[1][2];
    float a20 = m[2][0], a21 = m[2][1], a22 = m[2][2];
    float b01 = a22 * a11 - a12 * a21, b11 = -a22 * a10 + a12 * a20, b21 = a21 * a10 - a11 * a20;
    float det = a00 * b01 + a01 * b11 + a02 * b21;
    return mat3(b01, -a22 * a01 + a02 * a21, a12 * a01 - a02 * a11,
                b11, a22 * a00 - a02 * a20, -a12 * a00 + a02 * a10,
                b21, -a21 * a00 + a01 * a20, a11 * a00 - a01 * a10) / det;
}
mat4 inverse(mat4 m) {
    float a00 = m[0][0], a01 = m[0][1], a02 = m[0][2], a03 = m[0][3];
    float a10 = m[1][0], a11 = m[1][1], a12 = m[1][2], a13 = m[1][3];
    float a20 = m[2][0], a21 = m[2][1], a22 = m[2][2], a23 = m[2][3];
    float a30 = m[3][0], a31 = m[3][1], a32 = m[3][2], a33 = m[3][3];
    float b00 = a00 * a11 - a01 * a10, b01 = a00 * a12 - a02 * a10, b02 = a00 * a13 - a03 * a10;
    float b03 = a01 * a12 - a02 * a11, b04 = a01 * a13 - a03 * a11, b05 = a02 * a13 - a03 * a12;
    float b06 = a20 * a31 - a21 * a30, b07 = a20 * a32 - a22 * a30, b08 = a20 * a33 - a23 * a30;
    float b09 = a21 * a32 - a22 * a31, b10 = a21 * a33 - a23 * a31, b11 = a22 * a33 - a23 * a32;
    float det = b00 * b11 - b01 * b10 + b02 * b09 + b03 * b08 - b04 * b07 + b05 * b06;
    return mat4(a11 * b11 - a12 * b10 + a13 * b09, a02 * b10 - a01 * b11 - a03 * b09,
                a31 * b05 - a32 * b04 + a33 * b03, a22 * b04 - a21 * b05 - a23 * b03,
                a12 * b08 - a10 * b11 - a13 * b07, a00 * b11 - a02 * b08 + a03 * b07,
                a32 * b02 - a30 * b05 - a33 * b01, a20 * b05 - a22 * b02 + a23 * b01,
                a10 * b10 - a11 * b08 + a13 * b06, a01 * b08 - a00 * b10 - a03 * b06,
                a30 * b04 - a31 * b02 + a33 * b00, a21 * b02 - a20 * b04 - a23 * b00,
                a11 * b07 - a10 * b09 - a12 * b06, a00 * b09 - a01 * b07 + a02 * b06,
                a31 * b01 - a30 * b03 - a32 * b00, a20 * b03 - a21 * b01 + a22 * b00) / det;
}
)GLSL";

std::string trimmed(std::string_view l);

// A constant written with numbers, + - * / and brackets ("0.1 / 3"), as uniforms' initial values are.
struct ConstEval {
    std::string_view s;
    size_t i = 0;
    void ws() {
        while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;
    }
    std::optional<double> primary() {
        ws();
        if (i < s.size() && (s[i] == '-' || s[i] == '+')) {
            const bool neg = s[i++] == '-';
            auto v = primary();
            if (!v) return std::nullopt;
            return neg ? -*v : *v;
        }
        if (i < s.size() && s[i] == '(') {
            ++i;
            auto v = sum();
            ws();
            if (!v || i >= s.size() || s[i] != ')') return std::nullopt;
            ++i;
            return v;
        }
        const std::string rest(s.substr(i));
        char* end = nullptr;
        const double v = std::strtod(rest.c_str(), &end);
        if (end == rest.c_str()) return std::nullopt;
        i += size_t(end - rest.c_str());
        if (i < s.size() && (s[i] == 'f' || s[i] == 'F')) ++i;
        return v;
    }
    std::optional<double> product() {
        auto v = primary();
        for (ws(); v && i < s.size() && (s[i] == '*' || s[i] == '/'); ws()) {
            const char op = s[i++];
            auto r = primary();
            if (!r) return std::nullopt;
            v = op == '*' ? *v * *r : *v / *r;
        }
        return v;
    }
    std::optional<double> sum() {
        auto v = product();
        for (ws(); v && i < s.size() && (s[i] == '+' || s[i] == '-'); ws()) {
            const char op = s[i++];
            auto r = product();
            if (!r) return std::nullopt;
            v = op == '+' ? *v + *r : *v - *r;
        }
        return v;
    }
    std::optional<double> all() {
        auto v = sum();
        ws();
        return v && i == s.size() ? v : std::nullopt;
    }
};

// "uniform <type> <name> = <value>;" on one line: the uniform without its
// value (GLSL ES 1.0), and the value when it is a constant the game can set.
bool splitUniformInit(const std::string& line, std::string& out, std::vector<UniformDefault>* defaults) {
    const size_t u = line.find_first_not_of(" \t");
    if (u == std::string::npos || line.compare(u, 8, "uniform ") != 0) return false;
    const size_t eq = line.find('='), semi = line.find(';');
    if (eq == std::string::npos || semi == std::string::npos || eq > semi) return false;
    std::string decl = line.substr(0, eq);
    while (!decl.empty() && std::isspace((unsigned char)decl.back())) decl.pop_back();
    size_t n = decl.size();
    while (n > 0 && identChar(decl[n - 1])) --n;
    const std::string name = decl.substr(n);
    size_t t = n;
    while (t > 0 && std::isspace((unsigned char)decl[t - 1])) --t;
    size_t tb = t;
    while (tb > 0 && identChar(decl[tb - 1])) --tb;
    const std::string type = decl.substr(tb, t - tb);
    if (name.empty() || type.empty()) return false;
    out = decl + line.substr(semi);
    if (!defaults) return true;
    std::string value = line.substr(eq + 1, semi - eq - 1);
    UniformDefault d;
    d.name = name;
    d.isInt = type == "int" || type == "bool" || type.rfind("ivec", 0) == 0 || type.rfind("bvec", 0) == 0;
    const int count = type == "float" || type == "int" || type == "bool" ? 1
                      : (type.size() == 4 || type.size() == 5) && std::isdigit((unsigned char)type.back()) ? type.back() - '0'
                                                                                                        : 0;
    if (count == 0) return true;  // a matrix or a sampler: no value kept
    const size_t open = value.find('(');
    std::vector<std::string> parts;
    if (count > 1 && open != std::string::npos && value.rfind(')') != std::string::npos) {
        std::string inner = value.substr(open + 1, value.rfind(')') - open - 1);
        size_t a = 0;
        for (size_t c; (c = inner.find(',', a)) != std::string::npos; a = c + 1) parts.push_back(inner.substr(a, c - a));
        parts.push_back(inner.substr(a));
    } else {
        parts.push_back(value);
    }
    for (std::string& p : parts) {
        if (p.find("true") != std::string::npos) p = "1";
        else if (p.find("false") != std::string::npos) p = "0";
        auto v = ConstEval{p}.all();
        if (!v) return true;
        d.values.push_back(float(*v));
    }
    if (d.values.size() == 1 && count > 1) d.values.assign(size_t(count), d.values[0]);  // vec3(0.5): every component
    if (int(d.values.size()) == count) defaults->push_back(std::move(d));
    return true;
}

// Globals with an initial value that is not a constant ("vec2 uv =
// openfl_TextureCoordv;"), which GLSL ES 1.0 refuses: declared alone, and
// given their value first thing in main(), in their order.
std::string moveGlobalInits(const std::string& body) {
    static const char* kTypes[] = {"float", "int", "bool", "vec2", "vec3", "vec4", "ivec2", "ivec3", "ivec4",
                                   "bvec2", "bvec3", "bvec4", "mat2", "mat3", "mat4"};
    std::string out, inits;
    int depth = 0;
    bool block = false;  // inside /* */
    size_t p = 0;
    while (p < body.size()) {
        size_t e = body.find('\n', p);
        if (e == std::string::npos) e = body.size();
        std::string line = body.substr(p, e - p);
        p = e + 1;
        if (depth == 0 && !block) {
            const std::string t = trimmed(line);
            for (const char* ty : kTypes) {
                const size_t tl = std::strlen(ty);
                if (t.compare(0, tl, ty) != 0 || t.size() <= tl || !std::isspace((unsigned char)t[tl])) continue;
                const size_t eq = t.find('='), semi = t.find(';');
                if (eq == std::string::npos || semi == std::string::npos || eq > semi || t[eq + 1] == '=') break;
                std::string name = trimmed(std::string_view(t).substr(tl, eq - tl));
                bool simple = !name.empty();
                for (char c : name) simple &= identChar(c);
                if (!simple || t.find(',') < semi || t.find('(') < eq) break;  // several at once, or a function
                inits += name + " =" + t.substr(eq + 1, semi - eq) + "\n";
                line = std::string(ty) + " " + name + ";" + t.substr(semi + 1);
                break;
            }
        }
        for (size_t i = 0; i < line.size(); ++i) {
            if (block) {
                if (line.compare(i, 2, "*/") == 0) block = false, ++i;
            } else if (line.compare(i, 2, "//") == 0) {
                break;
            } else if (line.compare(i, 2, "/*") == 0) {
                block = true, ++i;
            } else if (line[i] == '{') {
                ++depth;
            } else if (line[i] == '}') {
                depth = depth > 0 ? depth - 1 : 0;
            }
        }
        out += line;
        if (e < body.size()) out += '\n';
    }
    if (inits.empty()) return body;
    // Into main(): after its opening brace.
    for (size_t m = out.find("main"); m != std::string::npos; m = out.find("main", m + 4)) {
        if ((m > 0 && identChar(out[m - 1])) || (m + 4 < out.size() && identChar(out[m + 4]))) continue;
        size_t b = m;
        while (b > 0 && std::isspace((unsigned char)out[b - 1])) --b;
        if (b < 4 || out.compare(b - 4, 4, "void") != 0) continue;
        const size_t brace = out.find('{', m);
        const size_t close = out.find(')', m);
        if (brace == std::string::npos || close == std::string::npos || close > brace) continue;
        out.insert(brace + 1, "\n" + inits);
        return out;
    }
    return body;  // no main() found: left as it was
}

// Does the shader define (or declare) a function of that name itself? The
// word before the name is then its type ("mat3 inverse(") - not "return".
bool definesFunction(const std::string& body, std::string_view name) {
    for (size_t p = body.find(name); p != std::string::npos; p = body.find(name, p + 1)) {
        const size_t e = p + name.size();
        if ((p > 0 && identChar(body[p - 1])) || (e < body.size() && identChar(body[e]))) continue;
        size_t k = e;
        while (k < body.size() && std::isspace((unsigned char)body[k])) ++k;
        if (k >= body.size() || body[k] != '(') continue;
        size_t b = p;
        while (b > 0 && std::isspace((unsigned char)body[b - 1])) --b;
        size_t a = b;
        while (a > 0 && identChar(body[a - 1])) --a;
        if (a < b && body.compare(a, b - a, "return") != 0) return true;
    }
    return false;
}

// Replaces whole-word occurrences of `from` (not inside other identifiers).
std::string replaceWord(const std::string& s, std::string_view from, std::string_view to) {
    std::string out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        if (s.compare(i, from.size(), from) == 0 && (i == 0 || !identChar(s[i - 1])) &&
            (i + from.size() >= s.size() || !identChar(s[i + from.size()]))) {
            out += to;
            i += from.size();
        } else {
            out += s[i++];
        }
    }
    return out;
}

std::string trimmed(std::string_view l) {
    size_t a = 0, b = l.size();
    while (a < b && std::isspace((unsigned char)l[a])) ++a;
    while (b > a && std::isspace((unsigned char)l[b - 1])) --b;
    return std::string(l.substr(a, b - a));
}

// "uniform sampler2D bitmap;" and friends are already in the prelude.
bool declaresBuiltin(const std::string& line) {
    std::string t = trimmed(line);
    for (const char* q : {"uniform ", "varying ", "attribute ", "in ", "out "}) {
        if (t.rfind(q, 0) != 0) continue;
        static const char* names[] = {"bitmap", "openfl_TextureCoordv", "openfl_TextureSize", "openfl_Alphav",
                                      "openfl_ColorMultiplierv", "openfl_ColorOffsetv", "openfl_HasColorTransform",
                                      "hasTransform", "hasColorTransform", "openfl_Position", "openfl_TextureCoord",
                                      "openfl_Alpha", "openfl_Matrix", "openfl_ColorMultiplier", "openfl_ColorOffset"};
        for (const char* n : names) {
            const size_t p = t.find(n);
            if (p == std::string::npos) continue;
            const size_t e = p + std::string_view(n).size();
            if ((p == 0 || !identChar(t[p - 1])) && (e >= t.size() || !identChar(t[e]))) return true;
        }
    }
    return false;
}

// Is `name` declared by the shader itself (a variable, uniform, parameter or
// macro), rather than used only as a call?
bool declaresName(const std::string& body, std::string_view name) {
    for (size_t p = body.find(name); p != std::string::npos; p = body.find(name, p + 1)) {
        const size_t e = p + name.size();
        if ((p > 0 && identChar(body[p - 1])) || (e < body.size() && identChar(body[e]))) continue;
        size_t k = e;
        while (k < body.size() && (body[k] == ' ' || body[k] == '\t')) ++k;
        if (k < body.size() && body[k] == '(') {
            // "#define texture(...)" declares it; a plain call does not.
            size_t b = p;
            while (b > 0 && (body[b - 1] == ' ' || body[b - 1] == '\t')) --b;
            if (b >= 7 && body.compare(b - 7, 7, "#define") == 0) return true;
            continue;
        }
        // Preceded by a type or #define: a declaration ("sampler2D texture", "#define texture x").
        size_t b = p;
        while (b > 0 && (body[b - 1] == ' ' || body[b - 1] == '\t')) --b;
        size_t a = b;
        while (a > 0 && (identChar(body[a - 1]) || body[a - 1] == '#')) --a;
        if (a < b) {
            const std::string word = body.substr(a, b - a);
            static const char* const kTypes[] = {"#define", "sampler2D", "float", "int", "bool", "vec2", "vec3", "vec4",
                                                 "mat2", "mat3", "mat4", "ivec2", "ivec3", "ivec4", "bvec2", "bvec3", "bvec4"};
            for (const char* t : kTypes)
                if (word == t) return true;
        }
    }
    return false;
}

// Shadertoy's entry point written as main: "void main(out vec4 c, in vec2 p)".
// It becomes gh_mainImage, called from a real main with the pixel position;
// what it writes to its colour comes out (unless it writes gl_FragColor itself).
std::string fixMainWithParameters(std::string body) {
    size_t p = 0;
    while ((p = body.find("main", p)) != std::string::npos) {
        const size_t e = p + 4;
        if ((p > 0 && identChar(body[p - 1])) || (e < body.size() && identChar(body[e]))) {
            p = e;
            continue;
        }
        size_t k = e;
        while (k < body.size() && std::isspace((unsigned char)body[k])) ++k;
        if (k >= body.size() || body[k] != '(') {
            p = e;
            continue;
        }
        const size_t close = body.find(')', k);
        if (close == std::string::npos) return body;
        const std::string params = trimmed(std::string_view(body).substr(k + 1, close - k - 1));
        if (params.empty() || params == "void") {
            p = e;
            continue;
        }
        // The colour parameter's name: the word after "out vec4".
        std::string colorName;
        const size_t outPos = params.find("out");
        if (outPos != std::string::npos) {
            size_t v = params.find("vec4", outPos);
            if (v != std::string::npos) {
                v += 4;
                while (v < params.size() && std::isspace((unsigned char)params[v])) ++v;
                size_t w = v;
                while (w < params.size() && identChar(params[w])) ++w;
                colorName = params.substr(v, w - v);
            }
        }
        body.replace(p, 4, "gh_mainImage");
        size_t bodyStart = body.find('{', close);
        bool writesColor = false;
        if (!colorName.empty() && bodyStart != std::string::npos) {
            for (size_t q = body.find(colorName, bodyStart); q != std::string::npos; q = body.find(colorName, q + 1)) {
                const size_t qe = q + colorName.size();
                if ((q > 0 && identChar(body[q - 1])) || (qe < body.size() && identChar(body[qe]))) continue;
                writesColor = true;
                break;
            }
        }
        body += "\nvoid main() {\n    vec4 gh_color = vec4(0.0);\n"
                "    gh_mainImage(gh_color, openfl_TextureCoordv * openfl_TextureSize);\n";
        if (writesColor) body += "    gl_FragColor = gh_color;\n";
        body += "}\n";
        return body;
    }
    return body;
}

}  // namespace

std::string translateShader(std::string_view src, Glsl target, std::vector<UniformDefault>* defaults) {
    const bool gles = target == Glsl::Es100;
    // Line pass: drop directives and declarations the prelude provides.
    std::string body;
    size_t p = 0;
    while (p <= src.size()) {
        size_t e = src.find('\n', p);
        if (e == std::string_view::npos) e = src.size();
        std::string line(src.substr(p, e - p));
        if (!line.empty() && line.back() == '\r') line.pop_back();
        p = e + 1;
        const std::string t = trimmed(line);
        if (t.rfind("#version", 0) == 0 || t.rfind("#extension", 0) == 0 || t == "#pragma header" ||
            t == "#pragma body" || t.rfind("#pragma ", 0) == 0 || declaresBuiltin(line) ||
            (target == Glsl::Legacy120 && t.rfind("precision ", 0) == 0)) {
            body += "\n";  // keep line numbers for error messages
            continue;
        }
        std::string bare;
        if (gles && splitUniformInit(line, bare, defaults)) line = bare;
        body += line + "\n";
        if (e == src.size()) break;
    }
    body = fixMainWithParameters(std::move(body));
    if (gles) body = moveGlobalInits(body);
    // GLSL ES 1.0 has no texture() builtin, so `texture` there is a user name;
    // a shader that only calls texture() (GLSL 1.30+) means texture2D.
    if (declaresName(body, "texture")) {
        body = replaceWord(body, "texture", "gh_userTexture");
    } else {
        std::string out;
        size_t i = 0;
        while (i < body.size()) {
            if (body.compare(i, 7, "texture") == 0 && (i == 0 || !identChar(body[i - 1])) &&
                (i + 7 >= body.size() || !identChar(body[i + 7]))) {
                out += "texture2D";
                i += 7;
                continue;
            }
            out += body[i++];
        }
        body.swap(out);
    }
    // texture2D(bitmap, ...) samples the sprite: flip and alpha handling.
    {
        std::string out;
        size_t i = 0;
        while (i < body.size()) {
            if (body.compare(i, 9, "texture2D") == 0 && (i == 0 || !identChar(body[i - 1])) &&
                (i + 9 >= body.size() || !identChar(body[i + 9]))) {
                size_t k = i + 9;
                while (k < body.size() && std::isspace((unsigned char)body[k])) ++k;
                if (k < body.size() && body[k] == '(') {
                    size_t a = k + 1;
                    while (a < body.size() && std::isspace((unsigned char)body[a])) ++a;
                    if (body.compare(a, 6, "bitmap") == 0 && a + 6 < body.size() && !identChar(body[a + 6])) {
                        size_t c = a + 6;
                        while (c < body.size() && std::isspace((unsigned char)body[c])) ++c;
                        if (c < body.size() && body[c] == ',') {
                            out += "gh_texBitmap(";
                            i = c + 1;
                            continue;
                        }
                    }
                }
                out += "gh_tex";
                i += 9;
                continue;
            }
            out += body[i++];
        }
        body.swap(out);
    }
    body = replaceWord(body, "texture2DLodEXT", "gh_texLod");
    body = replaceWord(body, "texture2DLod", "gh_texLod");
    body = replaceWord(body, "gl_FragColor", "gh_FragColor");
    body = replaceWord(body, "varying", "");
    // gl_FragData[0] -> gh_FragColor
    for (size_t q; (q = body.find("gl_FragData[0]")) != std::string::npos;) body.replace(q, 14, "gh_FragColor");
    // Shadertoy names some engines provide by themselves.
    std::string extra;
    auto uses = [&](std::string_view name) {
        for (size_t q = body.find(name); q != std::string::npos; q = body.find(name, q + 1)) {
            const size_t e = q + name.size();
            if ((q == 0 || !identChar(body[q - 1])) && (e >= body.size() || !identChar(body[e]))) return true;
        }
        return false;
    };
    if (uses("iResolution") && !declaresName(body, "iResolution")) extra += "#define iResolution vec3(openfl_TextureSize, 0.0)\n";
    if (uses("iTime") && !declaresName(body, "iTime")) extra += "uniform float iTime;\n";
    if (target != Glsl::Core330 && uses("inverse") && !declaresName(body, "inverse") && !definesFunction(body, "inverse"))
        extra += kInverse;
    if (!extra.empty()) extra += "#line 1\n";
    const char* prelude = gles ? kPreludeEs : target == Glsl::Legacy120 ? kPreludeGl120 : kPrelude;
    return std::string(prelude) + kCommon + extra + body;
}

}  // namespace gh
