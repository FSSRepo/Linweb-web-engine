#include "layout_engine.h"
#include "renderer/renderer.h"
#include <vector>
#include <memory>
#include <string>

namespace linweb
{

    /**
     * @brief Parses a string value to pixels, handling units such as %, px, rem and em.
     *
     * @param s The string to parse.
     * @param parent_size The size of the parent container for percentage or em calculations.
     * @return float The value converted to pixels.
     */
    namespace {
        // calc(): sums, differences, products and quotients of lengths, with parentheses. A percentage refers to
        // the reference size the caller passes, as it does for a plain percentage.
        struct CalcEvaluator
        {
            const std::string &s;
            size_t pos;
            float reference;

            void skip() { while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos]))) ++pos; }
            bool eat(char c) { skip(); if (pos < s.size() && s[pos] == c) { ++pos; return true; } return false; }

            float expression()
            {
                float v = term();
                for (;;) {
                    if (eat('+')) v += term();
                    else if (eat('-')) v -= term();
                    else return v;
                }
            }

            float term()
            {
                float v = factor();
                for (;;) {
                    if (eat('*')) v *= factor();
                    else if (eat('/')) { float d = factor(); v = d != 0.0f ? v / d : 0.0f; }
                    else return v;
                }
            }

            float factor()
            {
                if (eat('(')) { float v = expression(); eat(')'); return v; }
                skip();
                size_t start = pos;
                if (pos < s.size() && (s[pos] == '-' || s[pos] == '+')) ++pos;
                while (pos < s.size() && (std::isdigit(static_cast<unsigned char>(s[pos])) || s[pos] == '.')) ++pos;
                float number = std::stof(s.substr(start, pos - start));
                if (pos < s.size() && s[pos] == '%') { ++pos; return number / 100.0f * reference; }
                size_t unit_start = pos;
                while (pos < s.size() && std::isalpha(static_cast<unsigned char>(s[pos]))) ++pos;
                std::string unit = s.substr(unit_start, pos - unit_start);
                if (unit == "pt") return number * 96 / 72;
                if (unit == "rem") return number * 16.0f;
                if (unit == "em") return number * reference;
                if (unit == "vh") return number / 100.0f * (float)Renderer::get_viewport_height();
                if (unit == "vw") return number / 100.0f * (float)Renderer::get_viewport_width();
                return number;   // px, or a plain number used as a factor
            }
        };
    }

    float LayoutEngine::parse_px(const std::string &s, float parent_size)
    {
        if (s.empty())
            return 0;
        if (s.rfind("calc(", 0) == 0) {
            try {
                CalcEvaluator evaluator{s, 4, parent_size};
                return evaluator.factor();
            } catch (...) {
                return 0;
            }
        }
        try
        {
            if (s.back() == '%')
            {
                float val = std::stof(s.substr(0, s.size() - 1));
                return (val / 100.0f) * parent_size;
            }
            size_t px = s.find("px");
            if (px != std::string::npos)
                return std::stof(s.substr(0, px));
            size_t pt = s.find("pt");
            if (pt != std::string::npos) return std::stof(s.substr(0, pt)) * 96 / 72;
            size_t vh = s.find("vh");
            if (vh != std::string::npos)
                return std::stof(s.substr(0, vh)) / 100.0f * (float)Renderer::get_viewport_height();
            size_t vw = s.find("vw");
            if (vw != std::string::npos)
                return std::stof(s.substr(0, vw)) / 100.0f * (float)Renderer::get_viewport_width();
            size_t rem = s.find("rem");
            if (rem != std::string::npos)
                return std::stof(s.substr(0, rem)) * 16.0f;
            size_t em = s.find("em");
            if (em != std::string::npos)
                return std::stof(s.substr(0, em)) * parent_size;
            return std::stof(s);
        }
        catch (...)
        {
            return 0;
        }
    }

    /**
     * @brief LayoutBox constructor. Determines the box type and initial position based on the style.
     *
     * @param node The styled node to position.
     */
    LayoutBox::LayoutBox(std::shared_ptr<StyledNode> node)
        : styled_node(std::move(node)), box_type(BoxType::Block), last_line_width(0), total_y_offset(0), scroll_y(0), z_index(0)
    {

        position = PositionType::Static;

        if (styled_node->node->type() == NodeType::Element)
        {
            std::string pos = styled_node->value("position");
            if (pos == "relative")
                position = PositionType::Relative;
            else if (pos == "absolute")
                position = PositionType::Absolute;
            else if (pos == "fixed")
                position = PositionType::Fixed;

            std::string display = styled_node->value("display");
            if (display == "inline")
            {
                box_type = BoxType::Inline;
            }
            else if (display == "inline-block")
            {
                box_type = BoxType::InlineBlock;
            }
            else if (display == "grid")
            {
                box_type = BoxType::Grid;
            }
            else if (display == "flex")
            {
                box_type = BoxType::Flex;
            }
            else if (display.empty() && (styled_node->node->tag_name == "svg" || styled_node->node->tag_name == "img"))
            {
                // replaced elements are inline-level by default: they sit in a line with the text around them
                box_type = BoxType::InlineBlock;
            }
            else
            {
                box_type = BoxType::Block;
            }

            std::string z_str = styled_node->value("z-index");
            if (!z_str.empty()) {
                try { z_index = std::stoi(z_str); } catch(...) { z_index = 0; }
            }
        }
        else
        {
            box_type = BoxType::Inline; // Text nodes are always inline
        }
    }

    /**
     * @brief Lays out the children, delegating according to the box type (Grid, Flex, Block).
     *
     * @param container_dimensions Container dimensions.
     */
    void LayoutBox::layout_children(Dimensions container_dimensions)
    {
        float current_x_offset = 0;
        float current_y_offset = 0;
        float max_line_height = 0;
        float max_x_offset = 0;

        // Filter children into normal flow and positioned
        std::vector<std::shared_ptr<LayoutBox>> normal_flow_children;
        for (const auto &child : children)
        {
            if ((box_type == BoxType::Flex || box_type == BoxType::Grid) && child->styled_node->node->is_text()) {
                const auto text = std::static_pointer_cast<TextNode>(child->styled_node->node)->data;
                if (text.find_first_not_of(" \t\r\n\f") == std::string::npos) continue;
            }
            if (child->position != PositionType::Absolute && child->position != PositionType::Fixed)
            {
                normal_flow_children.push_back(child);
            }
        }

        if (styled_node->value("display") == "table")
        {
            layout_table(container_dimensions);
        }
        else if (box_type == BoxType::Grid)
        {
            layout_grid(container_dimensions, normal_flow_children);
        }
        else if (box_type == BoxType::Flex)
        {
            layout_flex(container_dimensions, normal_flow_children);
        }
        else
        {
            layout_block(container_dimensions, normal_flow_children);
        }
    }

    void LayoutBox::layout(Dimensions container_dimensions)
    {
        resolve_dimensions(container_dimensions);
        apply_relative_position(container_dimensions);
        layout_children(container_dimensions);
        center_button_content();
        layout_positioned_children();
    }

    std::shared_ptr<LayoutBox> LayoutEngine::build_layout_tree(const std::shared_ptr<StyledNode> &styled_node)
    {
        if (styled_node->value("display") == "none")
        {
            return nullptr;
        }

        auto root = std::make_shared<LayoutBox>(styled_node);

        // SVG elements are atomic in terms of layout tree; their children (raw text) shouldn't be laid out
        if (styled_node->node->tag_name == "svg")
        {
            return root;
        }

        for (const auto &child : styled_node->children)
        {
            auto layout_child = build_layout_tree(child);
            if (layout_child)
            {
                layout_child->parent = root;
                root->children.push_back(layout_child);
            }
        }

        // An inline wrapper containing only blocks establishes an anonymous
        // block flow, rather than shrinking all descendants onto one line.
        if (root->box_type == BoxType::Inline && !root->children.empty()) {
            bool only_blocks = true;
            for (const auto& child : root->children) {
                if (child->box_type == BoxType::Inline || child->box_type == BoxType::InlineBlock) only_blocks = false;
            }
            if (only_blocks) root->box_type = BoxType::Block;
        }
        return root;
    }

} // namespace linweb
