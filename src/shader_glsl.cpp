// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

#include "shader_glsl.hpp"

#include <cctype>
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

// OpenGL ES 2 (Android): GLSL ES 1.0, like the shaders themselves.
constexpr const char* kPreludeEs = R"GLSL(#version 100
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

std::string translateShader(std::string_view src, bool gles) {
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
            t == "#pragma body" || t.rfind("#pragma ", 0) == 0 || declaresBuiltin(line)) {
            body += "\n";  // keep line numbers for error messages
            continue;
        }
        body += line + "\n";
        if (e == src.size()) break;
    }
    body = fixMainWithParameters(std::move(body));
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
    if (!extra.empty()) extra += "#line 1\n";
    return std::string(gles ? kPreludeEs : kPrelude) + kCommon + extra + body;
}

}  // namespace gh
