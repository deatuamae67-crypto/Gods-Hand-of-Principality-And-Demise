// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

// Psych Engine / Flixel shaders are OpenFL fragment shaders in GLSL ES 1.0
// style ("#pragma header", bitmap, openfl_TextureCoordv, flixel_texture2D,
// gl_FragColor). translateShader() turns one into a GLSL 330 fragment shader
// (GLSL ES 100 for OpenGL ES 2, on Android; GLSL 120 for OpenGL 2.1, the
// Windows Vista/7 build for older graphics cards) that runs with raylib's
// default vertex shader.
//
// Uniforms the renderer sets on top of the shader's own:
//   openfl_TextureSize  size of the texture being drawn
//   gh_flipY            1 when drawing a render texture (stored upside down)
//   gh_premul           1 when the texture holds straight (not premultiplied) alpha
// The output is premultiplied, like OpenFL's, so it is drawn with
// premultiplied-alpha blending.
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace gh {

enum class Glsl { Core330, Es100, Legacy120 };

// GLSL ES 1.0 has no initial values for uniforms ("uniform float speed = 2.0;"):
// the translation drops them and lists them here, for the game to set once
// the shader is made.
struct UniformDefault {
    std::string name;
    std::vector<float> values;  // 1 to 4 components
    bool isInt = false;
};

std::string translateShader(std::string_view openflFragment, Glsl target, std::vector<UniformDefault>* defaults = nullptr);
inline std::string translateShader(std::string_view openflFragment, bool gles = false) {
    return translateShader(openflFragment, gles ? Glsl::Es100 : Glsl::Core330);
}

}  // namespace gh
