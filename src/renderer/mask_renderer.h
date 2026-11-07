#pragma once
#include <string>
#include <vector>
#include "gl_wrapper.h"

namespace linweb {

// One stop of a CSS mask-image linear gradient: the mask alpha at a distance along the gradient line (in px).
struct MaskStop {
    float position = 0.0f;
    float alpha = 1.0f;
};

// Reads "linear-gradient(to bottom, #000 0, #000 calc(100% - 48px), transparent 100%)" (and the "to top" form)
// for a box of height box_h. reversed is true for "to top". Returns false for anything else.
bool parse_mask_gradient(const std::string& value, float box_h, std::vector<MaskStop>& stops, bool& reversed);

// A mask layer: the children of a masked box are drawn into an offscreen target first. begin returns false when a
// layer is already open (nested masks draw without a layer); end restores the previous framebuffer.
bool mask_layer_begin(GLint& previous_framebuffer);
void mask_layer_end(GLint previous_framebuffer);

// Draws the layer over the box rectangle (x, y, w, h), fading it with the gradient alpha, in horizontal bands.
void mask_layer_compose(float x, float y, float w, float h, const std::vector<MaskStop>& stops, bool reversed);

} // namespace linweb

namespace linweb {

// Text-shadow layer: the glyphs of a shadowed text box are drawn into their own target with a max-alpha blend, so
// overlapping blurred glyphs do not add up. The layer is then composed once, with normal alpha blending, over whatever
// target is current (including a mask layer). begin returns false if the layer could not be opened.
bool text_shadow_layer_begin(GLint& previous_framebuffer);
void text_shadow_layer_end(GLint previous_framebuffer);
void text_shadow_layer_compose(float x, float y, float w, float h, float opacity);
// The layer's texture and size (used by the blur).
GLuint text_shadow_layer_texture();
int text_shadow_layer_width();
int text_shadow_layer_height();

} // namespace linweb

namespace linweb {

// Gaussian blur of the text-shadow layer with standard deviation sigma (in screen pixels; CSS blur radius / 2),
// done as two separable passes over the region (x, y, w, h): horizontal into an intermediate target, then vertical
// composited over the current target with the given opacity.
void text_shadow_layer_blur_compose(float x, float y, float w, float h, float sigma, float opacity);

// Cache of blurred text shadows. A blurred shadow is the same picture on every frame, so it is blurred once into a texture of
// its own size and each frame only draws that texture. The key says which picture it is (text, font, colour, blur); the
// anchor is the origin of the text, so the same shadow can be drawn wherever the text is.
std::string text_shadow_cache_key(const std::string& text, float font_size, bool bold, bool monospace, bool invert, const void* font,
                                  float r, float g, float b, float a, float sigma, float letter_spacing);
// The blur reaches past the window when the text is near an edge. A shadow is kept whole, so it is made a little way in:
// this gives how far to move it (whole pixels, 0 when it already fits) for the region (x, y, w, h). False when the shadow
// cannot be kept (bigger than the window or than the limit of the cache).
bool text_shadow_cache_shift(float x, float y, float w, float h, float sigma, float& shift_x, float& shift_y);
// Draws the cached shadow over the current target. False when there is none for the key.
bool text_shadow_cache_draw(const std::string& key, float anchor_x, float anchor_y, float opacity);
// After the sharp glyphs are in the text-shadow layer: blurs the region (x, y, w, h) of it into a cached texture. False when
// the shadow cannot be kept (it is cut by the window or too big); the caller then blurs it straight onto the target.
bool text_shadow_cache_store(const std::string& key, float anchor_x, float anchor_y, float x, float y, float w, float h, float sigma);
void text_shadow_cache_clear();

} // namespace linweb
