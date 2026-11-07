#include "renderer/mask_renderer.h"
#include "renderer/matrix_math.h"
#include "renderer/renderer_internal.h"
#include "renderer/shader_manager.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>

namespace linweb {

namespace {

// Vertex and fragment sources of one separable gaussian pass: taps along uStep, weights exp(-i^2 / (2 sigma^2)),
// up to uRadius taps each side. The fragment colour is premultiplied, so the sum of the taps is too.
const char* kBlurVertexSource =
    "attribute vec2 aPos;\n"
    "attribute vec2 aUV;\n"
    "uniform mat4 uProj;\n"
    "varying vec2 vUV;\n"
    "void main(){ vUV = aUV; gl_Position = uProj * vec4(aPos, 0.0, 1.0); }\n";

const char* kBlurFragmentSource =
    "#ifdef GL_ES\n"
    "precision mediump float;\n"
    "#endif\n"
    "uniform sampler2D uTex;\n"
    "uniform vec2 uStep;\n"
    "uniform float uRadius;\n"
    "uniform float uSigma;\n"
    "uniform float uOpacity;\n"
    "varying vec2 vUV;\n"
    "void main(){\n"
    "  float inv = 1.0 / (2.0 * uSigma * uSigma);\n"
    "  vec4 sum = vec4(0.0);\n"
    "  float wsum = 0.0;\n"
    "  for (int i = -64; i <= 64; ++i) {\n"
    "    float fi = float(i);\n"
    "    if (abs(fi) > uRadius) continue;\n"
    "    float w = exp(-fi * fi * inv);\n"
    "    sum += texture2D(uTex, vUV + uStep * fi) * w;\n"
    "    wsum += w;\n"
    "  }\n"
    "  gl_FragColor = (sum / wsum) * uOpacity;\n"
    "}\n";

GLuint g_blur_program = 0;
GLint g_blur_u_proj = -1, g_blur_u_tex = -1, g_blur_u_step = -1, g_blur_u_radius = -1, g_blur_u_sigma = -1, g_blur_u_opacity = -1;
GLint g_blur_a_pos = -1, g_blur_a_uv = -1;

GLuint g_blur_fbo = 0;
GLuint g_blur_tex = 0;
int g_blur_w = 0;
int g_blur_h = 0;

bool ensure_blur_program()
{
    if (g_blur_program) return true;
    static bool failed = false;
    if (failed) return false;
    GLuint vs = compile_shader(GL_VERTEX_SHADER, kBlurVertexSource);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, kBlurFragmentSource);
    if (vs && fs) g_blur_program = link_program(vs, fs);
    if (vs) glDeleteShader(vs);
    if (fs) glDeleteShader(fs);
    if (!g_blur_program) {
        failed = true;
        return false;
    }
    g_blur_u_proj = glGetUniformLocation(g_blur_program, "uProj");
    g_blur_u_tex = glGetUniformLocation(g_blur_program, "uTex");
    g_blur_u_step = glGetUniformLocation(g_blur_program, "uStep");
    g_blur_u_radius = glGetUniformLocation(g_blur_program, "uRadius");
    g_blur_u_sigma = glGetUniformLocation(g_blur_program, "uSigma");
    g_blur_u_opacity = glGetUniformLocation(g_blur_program, "uOpacity");
    g_blur_a_pos = glGetAttribLocation(g_blur_program, "aPos");
    g_blur_a_uv = glGetAttribLocation(g_blur_program, "aUV");
    return true;
}

// Draws the region (x, y, w, h) with the blur program. Texture coordinates follow the layer convention (layout row y
// is texture row height - y), so each pass samples the place it was written to.
void draw_blur_region(float x, float y, float w, float h, float step_x, float step_y, float radius, float sigma, float opacity)
{
    const float vw = static_cast<float>(g_blur_w);
    const float vh = static_cast<float>(g_blur_h);
    const float u0 = x / vw, u1 = (x + w) / vw;
    const float v_top = (vh - y) / vh, v_bottom = (vh - (y + h)) / vh;
    const float verts[] = {
        x,     y,     u0, v_top,
        x + w, y,     u1, v_top,
        x + w, y + h, u1, v_bottom,

        x,     y,     u0, v_top,
        x + w, y + h, u1, v_bottom,
        x,     y + h, u0, v_bottom
    };
    glUseProgram(g_blur_program);
    glUniformMatrix4fv(g_blur_u_proj, 1, GL_FALSE, RendererState::proj.data());
    glUniform1i(g_blur_u_tex, 0);
    glUniform2f(g_blur_u_step, step_x, step_y);
    glUniform1f(g_blur_u_radius, radius);
    glUniform1f(g_blur_u_sigma, sigma);
    glUniform1f(g_blur_u_opacity, opacity);
    glBindBuffer(GL_ARRAY_BUFFER, RendererState::vbo_color);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STREAM_DRAW);
    glEnableVertexAttribArray(g_blur_a_pos);
    glVertexAttribPointer(g_blur_a_pos, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(g_blur_a_uv);
    glVertexAttribPointer(g_blur_a_uv, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableVertexAttribArray(g_blur_a_pos);
    glDisableVertexAttribArray(g_blur_a_uv);
}

} // namespace

void text_shadow_layer_blur_compose(float x, float y, float w, float h, float sigma, float opacity)
{
    const GLuint source = text_shadow_layer_texture();
    if (source == 0 || w <= 0.0f || h <= 0.0f || sigma <= 0.0f) return;
    if (!ensure_blur_program()) return;

    const int vw = RendererState::viewport_width;
    const int vh = RendererState::viewport_height;
    // the source layer and the blur target have the viewport size
    if (text_shadow_layer_width() != vw || text_shadow_layer_height() != vh) return;

    if (!g_blur_fbo) glGenFramebuffers(1, &g_blur_fbo);
    if (!g_blur_tex) glGenTextures(1, &g_blur_tex);
    if (g_blur_w != vw || g_blur_h != vh) {
        glBindTexture(GL_TEXTURE_2D, g_blur_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, vw, vh, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        g_blur_w = vw;
        g_blur_h = vh;
    }

    // the gaussian reaches 3 sigma: the region is widened by that, so the glow is not cut at the edges
    const float reach = std::ceil(3.0f * sigma);
    const float radius = std::min(reach, 64.0f);
    const float rx = x - reach, ry = y - reach, rw = w + 2.0f * reach, rh = h + 2.0f * reach;

    GLint previous_framebuffer = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous_framebuffer);
    GLint scissor_box[4];
    glGetIntegerv(GL_SCISSOR_BOX, scissor_box);
    const GLboolean scissor_enabled = glIsEnabled(GL_SCISSOR_TEST);
    const GLboolean blend_enabled = glIsEnabled(GL_BLEND);
    GLint saved_src = GL_SRC_ALPHA, saved_dst = GL_ONE_MINUS_SRC_ALPHA;
    glGetIntegerv(GL_BLEND_SRC_RGB, &saved_src);
    glGetIntegerv(GL_BLEND_DST_RGB, &saved_dst);

    // the region in scissor coordinates (origin at the bottom of the window)
    glEnable(GL_SCISSOR_TEST);
    glScissor(static_cast<int>(std::floor(rx)), static_cast<int>(std::floor(vh - (ry + rh))),
              static_cast<int>(std::ceil(rw)) + 1, static_cast<int>(std::ceil(rh)) + 1);
    glDisable(GL_BLEND);

    // horizontal pass: the layer into the intermediate target
    glBindFramebuffer(GL_FRAMEBUFFER, g_blur_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_blur_tex, 0);
    glViewport(0, 0, vw, vh);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, source);
    draw_blur_region(rx, ry, rw, rh, 1.0f / static_cast<float>(vw), 0.0f, radius, sigma, 1.0f);

    // vertical pass: the blurred intermediate, composited over the previous target with the opacity
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previous_framebuffer));
    glViewport(0, 0, vw, vh);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glBindTexture(GL_TEXTURE_2D, g_blur_tex);
    draw_blur_region(rx, ry, rw, rh, 0.0f, 1.0f / static_cast<float>(vh), radius, sigma, opacity);

    // restore the state the caller had
    glUseProgram(0);
    glBlendFuncSeparate(static_cast<GLenum>(saved_src), static_cast<GLenum>(saved_dst), GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    if (blend_enabled) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (scissor_enabled) glScissor(scissor_box[0], scissor_box[1], scissor_box[2], scissor_box[3]);
    else glDisable(GL_SCISSOR_TEST);
}

// ---------------------------------------------------------------------------------------------------------------------
// Cache of blurred text shadows. A blurred shadow is the same picture on every frame (the same text, font, colour and blur),
// so it is made once into a small texture, the size of the shadow and not of the window, and each frame only draws that
// texture, the way a browser keeps what it has rasterized instead of blurring again on every frame. Entries are evicted
// by age when their textures pass a memory budget.
// ---------------------------------------------------------------------------------------------------------------------
namespace {

struct ShadowEntry {
    GLuint texture = 0;
    int width = 0;
    int height = 0;
    float rel_left = 0.0f;      // where the texture starts, relative to the anchor (the origin of the text) it was made for
    float rel_top = 0.0f;
    uint64_t used = 0;          // last time it was drawn
};

std::unordered_map<std::string, ShadowEntry> g_shadow_cache;
uint64_t g_shadow_clock = 0;
size_t g_shadow_bytes = 0;
const size_t kShadowBudgetBytes = 16u * 1024 * 1024;
const int kShadowMaxSide = 1024;          // a bigger shadow is not cached

// One target the size of the window where a shadow is blurred before its pixels are copied into its own texture.
GLuint g_scratch_fbo = 0;
GLuint g_scratch_tex = 0;
int g_scratch_w = 0;
int g_scratch_h = 0;

bool shadow_cache_enabled()
{
    static const bool enabled = [] {
        const char* value = std::getenv("LINWEB_SHADOW_CACHE");
        return !(value && value[0] == '0');
    }();
    return enabled;
}

// LINWEB_LOG_SHADOW_CACHE=1: one line each time a shadow is kept or cannot be kept (hits are silent)
bool log_shadow_cache()
{
    static const bool enabled = std::getenv("LINWEB_LOG_SHADOW_CACHE") != nullptr;
    return enabled;
}

void evict_shadows()
{
    while (g_shadow_bytes > kShadowBudgetBytes && !g_shadow_cache.empty()) {
        auto oldest = g_shadow_cache.begin();
        for (auto it = g_shadow_cache.begin(); it != g_shadow_cache.end(); ++it)
            if (it->second.used < oldest->second.used) oldest = it;
        glDeleteTextures(1, &oldest->second.texture);
        g_shadow_bytes -= static_cast<size_t>(oldest->second.width) * static_cast<size_t>(oldest->second.height) * 4u;
        g_shadow_cache.erase(oldest);
    }
}

bool ensure_scratch(int width, int height)
{
    if (!g_scratch_fbo) glGenFramebuffers(1, &g_scratch_fbo);
    if (!g_scratch_tex) glGenTextures(1, &g_scratch_tex);
    if (g_scratch_w == width && g_scratch_h == height) return true;
    glBindTexture(GL_TEXTURE_2D, g_scratch_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    GLint previous = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous);
    glBindFramebuffer(GL_FRAMEBUFFER, g_scratch_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_scratch_tex, 0);
    const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previous));
    g_scratch_w = width;
    g_scratch_h = height;
    return complete;
}

// Draws a cached shadow (premultiplied colour) over the current target, scaled by the opacity.
void draw_shadow_texture(const ShadowEntry& entry, float x, float y, float opacity)
{
    if (!RendererState::program_image || RendererState::vbo_color == 0 || entry.texture == 0) return;
    const float w = static_cast<float>(entry.width);
    const float h = static_cast<float>(entry.height);

    glUseProgram(RendererState::program_image);
    auto identity = mat4_identity();
    glUniformMatrix4fv(RendererState::program_image_u_model, 1, GL_FALSE, identity.data());
    glUniform1f(RendererState::program_image_u_radius, 0.0f);
    glUniform2f(RendererState::program_image_u_size, w, h);
    if (RendererState::program_image_u_shadow_offset >= 0) glUniform2f(RendererState::program_image_u_shadow_offset, 0.0f, 0.0f);
    if (RendererState::program_image_u_shadow_blur >= 0) glUniform1f(RendererState::program_image_u_shadow_blur, 0.0f);
    if (RendererState::program_image_u_shadow_color >= 0) glUniform4f(RendererState::program_image_u_shadow_color, 0.0f, 0.0f, 0.0f, 0.0f);
    glUniform1f(glGetUniformLocation(RendererState::program_image, "uOpacity"), opacity);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, entry.texture);
    glUniform1i(RendererState::program_image_u_tex, 0);

    // same blending as the layer compose: the colour is premultiplied, the opacity scales it through the blend colour
    GLint src_rgb = GL_SRC_ALPHA, dst_rgb = GL_ONE_MINUS_SRC_ALPHA, src_a = GL_ONE, dst_a = GL_ONE_MINUS_SRC_ALPHA;
    glGetIntegerv(GL_BLEND_SRC_RGB, &src_rgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &dst_rgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &src_a);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &dst_a);
    glBlendColor(opacity, opacity, opacity, opacity);
    glBlendFuncSeparate(GL_CONSTANT_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    // the texture rows start at the bottom of the region (it was copied from a framebuffer)
    const float verts[] = {
        x,     y,     0.0f, 1.0f,
        x + w, y,     1.0f, 1.0f,
        x + w, y + h, 1.0f, 0.0f,

        x,     y,     0.0f, 1.0f,
        x + w, y + h, 1.0f, 0.0f,
        x,     y + h, 0.0f, 0.0f
    };
    glBindBuffer(GL_ARRAY_BUFFER, RendererState::vbo_color);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STREAM_DRAW);
    glEnableVertexAttribArray(RendererState::program_image_a_pos);
    glVertexAttribPointer(RendererState::program_image_a_pos, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(RendererState::program_image_a_uv);
    glVertexAttribPointer(RendererState::program_image_a_uv, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glDrawArrays(GL_TRIANGLES, 0, 6);

    glBlendFuncSeparate(static_cast<GLenum>(src_rgb), static_cast<GLenum>(dst_rgb), static_cast<GLenum>(src_a), static_cast<GLenum>(dst_a));
    glDisableVertexAttribArray(RendererState::program_image_a_pos);
    glDisableVertexAttribArray(RendererState::program_image_a_uv);
    glUseProgram(0);
}

} // namespace

std::string text_shadow_cache_key(const std::string& text, float font_size, bool bold, bool monospace, bool invert, const void* font,
                                  float r, float g, float b, float a, float sigma, float letter_spacing)
{
    char head[192];
    std::snprintf(head, sizeof(head), "%.2f|%d%d%d|%p|%.3f,%.3f,%.3f,%.3f|%.2f|%.2f|", font_size, bold ? 1 : 0, monospace ? 1 : 0,
                  invert ? 1 : 0, font, r, g, b, a, sigma, letter_spacing);
    return std::string(head) + text;
}

bool text_shadow_cache_shift(float x, float y, float w, float h, float sigma, float& shift_x, float& shift_y)
{
    shift_x = 0.0f;
    shift_y = 0.0f;
    if (!shadow_cache_enabled() || sigma <= 0.0f || w <= 0.0f || h <= 0.0f) return false;
    const int vw = RendererState::viewport_width;
    const int vh = RendererState::viewport_height;
    const float reach = std::ceil(3.0f * sigma);
    const int x0 = static_cast<int>(std::floor(x - reach));
    const int y0 = static_cast<int>(std::floor(y - reach));
    const int x1 = static_cast<int>(std::ceil(x + w + reach)) + 1;
    const int y1 = static_cast<int>(std::ceil(y + h + reach)) + 1;
    const int cw = x1 - x0;
    const int ch = y1 - y0;
    if (cw <= 0 || ch <= 0 || cw > kShadowMaxSide || ch > kShadowMaxSide || cw > vw || ch > vh) return false;
    if (x0 < 0) shift_x = static_cast<float>(-x0);
    else if (x1 > vw) shift_x = static_cast<float>(vw - x1);
    if (y0 < 0) shift_y = static_cast<float>(-y0);
    else if (y1 > vh) shift_y = static_cast<float>(vh - y1);
    return ensure_scratch(vw, vh);
}

bool text_shadow_cache_draw(const std::string& key, float anchor_x, float anchor_y, float opacity)
{
    if (!shadow_cache_enabled()) return false;
    auto found = g_shadow_cache.find(key);
    if (found == g_shadow_cache.end()) return false;
    found->second.used = ++g_shadow_clock;
    draw_shadow_texture(found->second, anchor_x + found->second.rel_left, anchor_y + found->second.rel_top, opacity);
    return true;
}

bool text_shadow_cache_store(const std::string& key, float anchor_x, float anchor_y, float x, float y, float w, float h, float sigma)
{
    if (!shadow_cache_enabled() || sigma <= 0.0f || w <= 0.0f || h <= 0.0f) return false;
    const int vw = RendererState::viewport_width;
    const int vh = RendererState::viewport_height;
    if (text_shadow_layer_texture() == 0 || text_shadow_layer_width() != vw || text_shadow_layer_height() != vh) return false;

    // the blur widens its region by 3 sigma on every side: that is what has to be kept
    const float reach = std::ceil(3.0f * sigma);
    const int x0 = static_cast<int>(std::floor(x - reach));
    const int y0 = static_cast<int>(std::floor(y - reach));
    const int x1 = static_cast<int>(std::ceil(x + w + reach)) + 1;
    const int y1 = static_cast<int>(std::ceil(y + h + reach)) + 1;
    // a shadow that is cut by the window would be kept cut: it is drawn the long way every time
    if (x0 < 0 || y0 < 0 || x1 > vw || y1 > vh) {
        if (log_shadow_cache()) std::fprintf(stderr, "shadow cache: not kept, cut by the window (%d,%d %dx%d in %dx%d)\n", x0, y0, x1 - x0, y1 - y0, vw, vh);
        return false;
    }
    const int cw = x1 - x0;
    const int ch = y1 - y0;
    if (cw <= 0 || ch <= 0 || cw > kShadowMaxSide || ch > kShadowMaxSide) return false;
    if (!ensure_scratch(vw, vh)) return false;

    GLint previous_framebuffer = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous_framebuffer);
    const GLboolean scissor_enabled = glIsEnabled(GL_SCISSOR_TEST);
    GLint scissor_box[4];
    glGetIntegerv(GL_SCISSOR_BOX, scissor_box);
    GLint saved_viewport[4];
    glGetIntegerv(GL_VIEWPORT, saved_viewport);

    // blur the layer over a cleared window-sized target...
    glBindFramebuffer(GL_FRAMEBUFFER, g_scratch_fbo);
    glViewport(0, 0, vw, vh);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    text_shadow_layer_blur_compose(x, y, w, h, sigma, 1.0f);

    // ...and keep only the pixels of the shadow, in a texture of their own
    ShadowEntry entry;
    glGenTextures(1, &entry.texture);
    glBindTexture(GL_TEXTURE_2D, entry.texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, cw, ch, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, x0, vh - y1, cw, ch);   // framebuffer rows count from the bottom

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previous_framebuffer));
    glViewport(saved_viewport[0], saved_viewport[1], saved_viewport[2], saved_viewport[3]);
    if (scissor_enabled) {
        glEnable(GL_SCISSOR_TEST);
        glScissor(scissor_box[0], scissor_box[1], scissor_box[2], scissor_box[3]);
    }

    entry.width = cw;
    entry.height = ch;
    entry.rel_left = static_cast<float>(x0) - anchor_x;
    entry.rel_top = static_cast<float>(y0) - anchor_y;
    entry.used = ++g_shadow_clock;
    auto old = g_shadow_cache.find(key);
    if (old != g_shadow_cache.end()) {
        glDeleteTextures(1, &old->second.texture);
        g_shadow_bytes -= static_cast<size_t>(old->second.width) * static_cast<size_t>(old->second.height) * 4u;
        g_shadow_cache.erase(old);
    }
    g_shadow_bytes += static_cast<size_t>(cw) * static_cast<size_t>(ch) * 4u;
    g_shadow_cache[key] = entry;
    if (log_shadow_cache())
        std::fprintf(stderr, "shadow cache: kept %dx%d (%.0f KB), %zu shadows, %.0f KB in all\n", cw, ch, cw * ch * 4 / 1024.0,
                     g_shadow_cache.size(), g_shadow_bytes / 1024.0);
    evict_shadows();
    return true;
}

void text_shadow_cache_clear()
{
    for (auto& item : g_shadow_cache) glDeleteTextures(1, &item.second.texture);
    g_shadow_cache.clear();
    g_shadow_bytes = 0;
}

} // namespace linweb
