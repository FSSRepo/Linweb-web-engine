#include "renderer/background_renderer.h"
#include "renderer/renderer.h"
#include "renderer/renderer_internal.h"
#include "renderer/texture_manager.h"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace linweb {
void draw_background_image(const LayoutBox& box, const Rect& area) {
    std::string value = box.styled_node->value("background-image");
    if (value.empty()) value = box.styled_node->value("background");
    size_t begin = value.find("url(");
    if (begin == std::string::npos || area.width <= 0 || area.height <= 0) return;
    begin += 4;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) ++begin;
    if (begin == value.size()) return;
    char quote = value[begin] == '\'' || value[begin] == '"' ? value[begin++] : 0;
    size_t end = value.find(quote ? quote : ')', begin);
    if (end == std::string::npos) return;
    std::string path = value.substr(begin, end - begin);
    if (path.empty() || path.find("://") != std::string::npos || path.compare(0, 5, "data:") == 0) return;
    Texture texture;
    if (path.size() >= 4 && path.substr(path.size() - 4) == ".svg")
        texture = TextureManager::get().get_svg_texture(path, static_cast<int>(area.width), static_cast<int>(area.height), box.styled_node->value("color"));
    else texture = TextureManager::get().get_texture(path);
    if (!texture.id || texture.width <= 0 || texture.height <= 0) return;
    float width = static_cast<float>(texture.width), height = static_cast<float>(texture.height);
    std::string size = box.styled_node->value("background-size");
    if (size == "cover" || size == "contain") {
        float scale = size == "cover" ? std::max(area.width / width, area.height / height) : std::min(area.width / width, area.height / height);
        width *= scale; height *= scale;
    } else if (!size.empty() && size != "auto") {
        std::istringstream parts(size);
        std::string first, second;
        parts >> first >> second;
        if (first != "auto") {
            width = LayoutEngine::parse_px(first, area.width);
            height *= width / texture.width;
        }
        if (!second.empty() && second != "auto") {
            height = LayoutEngine::parse_px(second, area.height);
            if (first == "auto") width = height * texture.width / texture.height;
        }
    }
    if (width < 0.1f || height < 0.1f) return;
    std::string position = box.styled_node->value("background-position");
    std::string repeat = box.styled_node->value("background-repeat");
    // Common legacy shorthand: url(...) x y repeat-x/no-repeat.
    if (position.empty()) {
        size_t close = value.find(')', end);
        if (!quote) close = end;
        if (close != std::string::npos) {
            std::istringstream tail(value.substr(close + 1));
            std::string token;
            while (tail >> token) {
                if (token == "repeat" || token == "repeat-x" || token == "repeat-y" || token == "no-repeat") { if (repeat.empty()) repeat = token; }
                else if (token == "center" || token == "left" || token == "right" || token == "top" || token == "bottom" || token[0] == '-' || token[0] == '.' || std::isdigit(static_cast<unsigned char>(token[0]))) {
                    if (!position.empty()) position += ' ';
                    position += token;
                }
            }
        }
    }
    std::istringstream positions(position);
    std::string horizontal = "0", vertical = "0";
    positions >> horizontal;
    if (!(positions >> vertical)) vertical = horizontal == "center" ? "center" : "0";
    if (horizontal == "top" || horizontal == "bottom") { std::swap(horizontal, vertical); if (horizontal == "0") horizontal = "center"; }
    auto offset = [](const std::string& token, float extra) {
        if (token == "center") return extra / 2;
        if (token == "right" || token == "bottom") return extra;
        if (token == "left" || token == "top") return 0.0f;
        return LayoutEngine::parse_px(token, extra);
    };
    float x = area.x + offset(horizontal, area.width - width), y = area.y + offset(vertical, area.height - height);
    bool repeat_x = repeat.empty() || repeat == "repeat" || repeat == "repeat-x";
    bool repeat_y = repeat.empty() || repeat == "repeat" || repeat == "repeat-y";
    if (repeat_x) x = area.x + std::fmod(x - area.x, width) - width;
    if (repeat_y) y = area.y + std::fmod(y - area.y, height) - height;
    GLint previous[4] = {0, 0, RendererState::viewport_width, RendererState::viewport_height};
    GLboolean enabled = glIsEnabled(GL_SCISSOR_TEST);
    if (enabled) glGetIntegerv(GL_SCISSOR_BOX, previous);
    int left = std::max(previous[0], static_cast<int>(std::floor(area.x)));
    int bottom = std::max(previous[1], RendererState::viewport_height - static_cast<int>(std::ceil(area.y + area.height)));
    int right = std::min(previous[0] + previous[2], static_cast<int>(std::ceil(area.x + area.width)));
    int top = std::min(previous[1] + previous[3], RendererState::viewport_height - static_cast<int>(std::floor(area.y)));
    glEnable(GL_SCISSOR_TEST);
    glScissor(left, bottom, std::max(0, right - left), std::max(0, top - bottom));
    unsigned tiles = 0;
    for (float ty = y; ty < area.y + area.height; ty += height) {
        for (float tx = x; tx < area.x + area.width; tx += width) {
            if (++tiles > 16384) break;
            Renderer::draw_image(texture.id, tx, ty, width, height, 0);
            if (!repeat_x) break;
        }
        if (!repeat_y || tiles > 16384) break;
    }
    if (enabled) glScissor(previous[0], previous[1], previous[2], previous[3]);
    else glDisable(GL_SCISSOR_TEST);
}
}
