#include "debug/frame_capture.h"
#include "gl_wrapper.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <string>
#include <vector>
#include "layout/layout_engine.h"
#include <functional>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace linweb {
bool dump_layout(const std::string& path, const LayoutBox& root) {
    std::ofstream out(path);
    if (!out) return false;
    out << "depth\ttag\tid\tx\ty\twidth\theight\tfont-size\tdisplay\talign\tmin-width\ttotal-height\n";
    std::function<void(const LayoutBox&, int)> visit = [&](const LayoutBox& box, int depth) {
        const auto& s = box.styled_node;
        out << depth << '\t' << s->node->tag_name << '\t' << s->node->data.id() << '\t'
            << box.dimensions.content.x << '\t' << box.dimensions.content.y << '\t'
            << box.dimensions.content.width << '\t' << box.dimensions.content.height << '\t'
            << s->value("font-size") << '\t' << s->value("display") << '\t' << s->value("text-align") << '\t' << s->value("min-width") << '\t' << box.total_content_height << '\n';
        for (const auto& child : box.children) visit(*child, depth + 1);
    };
    visit(root, 0);
    return out.good();
}
bool capture_frame(const std::string& path, int width, int height) {
    if (width <= 0 || height <= 0) return false;
    std::vector<unsigned char> pixels(static_cast<size_t>(width) * height * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    std::string extension = path.size() >= 4 ? path.substr(path.size() - 4) : std::string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (extension == ".png") {
        stbi_flip_vertically_on_write(1);
        return stbi_write_png(path.c_str(), width, height, 3, pixels.data(), width * 3) != 0;
    }
    if (extension == ".jpg" || (path.size() >= 5 && extension == "jpeg" && path[path.size() - 5] == '.')) {
        stbi_flip_vertically_on_write(1);
        return stbi_write_jpg(path.c_str(), width, height, 3, pixels.data(), 90) != 0;
    }
    return false;
}
}
