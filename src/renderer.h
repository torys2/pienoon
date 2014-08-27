// Copyright 2014 Google Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef RENDERER_H
#define RENDERER_H

namespace fpl {

using mathfu::vec2i;
using mathfu::vec2;
using mathfu::vec3;
using mathfu::vec4;
using mathfu::mat4;

class Renderer;

enum BlendMode {
  kBlendModeOff,
  kBlendModeTest,
  kBlendModeAlpha,

  kBlendModeCount // Must be at end.
};
static const int kMaxTexturesPerShader = 8;

// Represents a shader consisting of a vertex and pixel shader. Also stores
// ids of standard uniforms. Use the Renderer class below to create these.
class Shader {
 public:

  Shader(GLuint program, GLuint vs, GLuint ps)
    : program_(program), vs_(vs), ps_(ps),
      uniform_model_view_projection_(-1),
      uniform_model_(-1),
      uniform_color_(-1),
      uniform_light_pos_(-1),
      uniform_camera_pos_(-1)
  {}

  ~Shader() {
    if (vs_) glDeleteShader(vs_);
    if (ps_) glDeleteShader(ps_);
    if (program_) glDeleteProgram(program_);
  }

  // Will make this shader active for any subsequent draw calls, and sets
  // all standard uniforms (e.g. mvp matrix) based on current values in
  // Renderer, if this shader refers to them.
  void Set(const Renderer &renderer) const;

  // Find a non-standard uniform by name, -1 means not found.
  GLint FindUniform(const char *uniform_name) {
    return glGetUniformLocation(program_, uniform_name);
  }
  // Set an non-standard uniform to a vec2/3/4 value. Call this
  // before Set() above
  template<int N> void SetUniform(GLint uniform_loc,
                                  const mathfu::Vector<float, N> &value) {
    // This should amount to a compile-time if-then.
    if (N == 2) glUniform2fv(uniform_loc, 1, &value[0]);
    else if(N == 3) glUniform3fv(uniform_loc, 1, &value[0]);
    else if(N == 4) glUniform4fv(uniform_loc, 1, &value[0]);
    else assert(0);
  }
  // Convenience call that does a Lookup and a Set if found.
  template<int N> bool SetUniform(const char *uniform_name,
                                  const mathfu::Vector<float, N> &value) {
    auto loc = FindUniform(uniform_name);
    if (loc < 0) return false;
    SetUniform(loc, value);
    return true;
  }

 private:

  friend class Renderer;

  GLuint program_, vs_, ps_;

  void Initialize();

  GLint uniform_model_view_projection_;
  GLint uniform_model_;
  GLint uniform_color_;
  GLint uniform_light_pos_;
  GLint uniform_camera_pos_;
};

struct Texture {
  Texture(GLuint _id, const vec2i &_size) : id(_id), size(_size) {}

  GLuint id;
  vec2i size;
};

class Material {
 public:
  Material() : shader_(nullptr), blend_mode_(kBlendModeOff) {}

  void Set(Renderer &renderer);
  void SetTextures();

  Shader *get_shader() { return shader_; }
  void set_shader(Shader *s) { shader_ = s; }
  std::vector<Texture *> &textures() { return textures_; }
  const std::vector<Texture *> &textures() const { return textures_; }
  int blend_mode() const { return blend_mode_; }
  void set_blend_mode(BlendMode blend_mode) {
    assert(0 <= blend_mode && blend_mode < kBlendModeCount);
    blend_mode_ = blend_mode;
  }

 private:
  Shader *shader_;
  std::vector<Texture *> textures_;
  BlendMode blend_mode_;
};

// An array of these enums defines the format of vertex data.
enum Attribute {
  kEND = 0,     // The array must always be terminated by one of these.
  kPosition3f,
  kNormal3f,
  kTangent4f,
  kTexCoord2f,
  kColor4ub
};

// A mesh instance contains a VBO and one or more IBO's.
class Mesh {
 public:
  // Initialize a Mesh by creating one VBO, and no IBO's.
  Mesh(const void *vertex_data, int count, int vertex_size,
      const Attribute *format);
  ~Mesh();

  // Create one IBO to be part of this mesh. May be called more than once.
  void AddIndices(const int *indices, int count, Material *mat);

  // Render itself. Uniforms must have been set before calling this.
  void Render(Renderer &renderer, bool ignore_material = false);

  // Get the material associated with the Nth IBO.
  Material *GetMaterial(int i) { return indices_[i].mat; }

  // Renders primatives using vertex and index data directly in local memory.
  // This is a convenient alternative to creating a Mesh instance for small
  // amounts of data, or dynamic data.
  static void RenderArray(GLenum primitive, int index_count,
                          const Attribute *format,
                          int vertex_size, const char *vertices,
                          const int *indices);

  // Convenience method for rendering a Quad. bottom_left and top_right must
  // have their X coordinate be different, but either Y or Z can be the same.
  static void RenderAAQuadAlongX(const vec3 &bottom_left,
                                 const vec3 &top_right,
                                 const vec2 &tex_bottom_left = vec2(0, 0),
                                 const vec2 &tex_top_right = vec2(1, 1));
 private:
  static void SetAttributes(GLuint vbo, const Attribute *attributes,
                            int vertex_size, const char *buffer);
  static void UnSetAttributes(const Attribute *attributes);
  struct Indices {
    int count;
    GLuint ibo;
    Material *mat;
  };
  std::vector<Indices> indices_;
  size_t vertex_size_;
  const Attribute *format_;
  GLuint vbo_;
};

// The core of the rendering system. Deals with setting up and shutting down
// the window + OpenGL context (based on SDL), and creating/using resources
// such as shaders, textures, and geometry.
class Renderer {
 public:
  // Creates the window + OpenGL context.
  // A descriptive error is in last_error() if it returns false.
  bool Initialize(const vec2i &window_size = vec2i(800, 600),
                           const char *window_title = "");

  // Swaps frames. Call this once per frame inside your main loop.
  void AdvanceFrame(bool minimized);

  // Cleans up whatever Initialize creates.
  void ShutDown();

  // Clears the framebuffer. Call this after AdvanceFrame if desired.
  void ClearFrameBuffer(const vec4 &color);

  // Create a shader object from two strings containing glsl code.
  // Returns NULL upon error, with a descriptive message in glsl_error().
  // Attribute names in the vertex shader should be aPosition, aNormal,
  // aTexCoord and aColor to match whatever attributes your vertex data has.
  Shader *CompileAndLinkShader(const char *vs_source, const char *ps_source);

  // Create a texture from a memory buffer containing xsize * ysize RGBA pixels.
  // Does not fail.
  Texture *CreateTexture(const uint8_t *buffer, const vec2i &size);

  // Create a texture from a memory buffer containing a TGA format file.
  // May only be uncompressed RGB or RGBA data. Returns NULL if the format is
  // not understood.
  Texture *CreateTextureFromTGAMemory(const void *tga_buf);

  // Set alpha test (cull pixels with alpha below amount) vs alpha blend
  // (blend with framebuffer pixel regardedless).
  // blend_mode: see materials.fbs for valid enum values.
  void SetBlendMode(BlendMode blend_mode, float amount = 0.5f);

  // Set to compare fragment against Z-buffer before writing, or not.
  void DepthTest(bool on);

  Renderer() : model_view_projection_(mat4::Identity()),
               model_(mat4::Identity()), color_(mathfu::kOnes4f),
               light_pos_(mathfu::kZeros3f), camera_pos_(mathfu::kZeros3f),
               window_size_(mathfu::kZeros2i), window_(nullptr),
               context_(nullptr) {}
  ~Renderer() { ShutDown(); }

  mat4 &model_view_projection() { return model_view_projection_; }
  const mat4 &model_view_projection() const { return model_view_projection_; }

  mat4 &model() { return model_; }
  const mat4 &model() const { return model_; }

  vec4 &color() { return color_; }
  const vec4 &color() const { return color_; }

  vec3 &light_pos() { return light_pos_; }
  const vec3 &light_pos() const { return light_pos_; }

  vec3 &camera_pos() { return camera_pos_; }
  const vec3 &camera_pos() const { return camera_pos_; }

  std::string &last_error() { return last_error_; }
  const std::string &last_error() const { return last_error_; }

  vec2i &window_size() { return window_size_; }
  const vec2i &window_size() const { return window_size_; }

 private:
  GLuint CompileShader(GLenum stage, GLuint program, const GLchar *source);

  // The mvp. Use the Ortho() and Perspective() methods in mathfu::Matrix
  // to conveniently change the camera.
  mat4 model_view_projection_;
  mat4 model_;
  vec4 color_;
  vec3 light_pos_;
  vec3 camera_pos_;

  vec2i window_size_;

  std::string last_error_;

  SDL_Window *window_;
  SDL_GLContext context_;

  BlendMode blend_mode_;
};

}  // namespace fpl

#endif  // RENDERER_H