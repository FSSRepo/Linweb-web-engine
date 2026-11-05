#include "gl_wrapper.h"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <map>
#include <cstdio>

#include "core/dom.h"
#include "core/dom_helpers.h"
#include "core/navigator.h"
#include "parser/html/html_parser.h"
#include "parser/css/css_parser.h"
#include "style/style_engine.h"
#include "style/animation_engine.h"
#include "layout/layout_engine.h"
#include "renderer/renderer.h"
#include "scripting/js_engine.h"
#include "scripting/dom_bindings.h"
#include "scripting/style_bindings.h"
#include "scripting/console_bindings.h"
#include "utils/file_utils.h"
#include "utils/page_resources.h"
#include "utils/dom_traversal.h"
#include "utils/layout_traversal.h"
#include "utils/scroll_manager.h"
#include "platform/window.h"
#include "platform/keyboard_handler.h"
#include "forms/form_bindings.h"
#include "forms/form_controls.h"
#include "platform/scroll_handler.h"
#include "platform/mouse_handler.h"
#include "debug/debug_visualizer.h"
#include "debug/node_tree_view.h"
#include "debug/frame_capture.h"
#include "network/http_client.h"
#include "network/page_downloader.h"
#include "utils/log.h"
#include <cctype>
#if !defined(WESTON_PLATFORM) && !defined(XORG_PLATFORM)
#include <GLFW/glfw3.h>
#endif

namespace linweb {

// Global state
std::shared_ptr<Node> g_dom_root;
std::shared_ptr<StyledNode> g_style_tree;
std::shared_ptr<LayoutBox> g_layout_tree;
std::shared_ptr<StyledNode> g_hovered_node;
std::shared_ptr<Node> g_focused_node;
std::shared_ptr<Node> g_active_node;
std::map<std::shared_ptr<Node>, float> g_scroll_states;
std::vector<std::shared_ptr<Node>> g_detached_nodes;
bool g_dom_dirty = false;
bool g_show_node_tree = false;
bool g_debug_mode_active = false;
std::shared_ptr<Node> g_debug_hovered_node = nullptr;
JSEngine* g_js_engine = nullptr;
float g_scroll_speed = 40.0f;

} // namespace linweb

using namespace linweb;

void update_animations_on_tree(const std::shared_ptr<StyledNode>& node, const StyleSheet& stylesheet, double current_time, float w, float h) {
    if (!node) return;
    StyleEngine::build_style_tree(node->node, stylesheet, g_hovered_node ? g_hovered_node->node : nullptr, g_focused_node, g_active_node, current_time, w, h);
    for (const auto& child : node->children) {
        update_animations_on_tree(child, stylesheet, current_time, w, h);
    }
}

// downloads/<host and path of the URL>, so each page keeps its own copy of its files
std::string download_directory_for(const std::string& url) {
    size_t scheme = url.find("://");
    std::string name = scheme == std::string::npos ? url : url.substr(scheme + 3);
    for (char& c : name) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '-') c = '_';
    }
    if (name.size() > 80) name.resize(80);
    return "downloads/" + name;
}

int main(int argc, char** argv) {
    DebugType debug_mode = DebugType::None;
    std::string html_file = "assets/pages/index.html";
    bool file_specified = false;
    bool static_mode = false;
    std::string screenshot_path;
    double screenshot_delay = 0.0;
    std::string layout_dump_path;
    std::string user_css_path;
    int window_width = 800, window_height = 600;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--screenshot-live") {
            static_mode = false;
        } else if (arg.compare(0, 19, "--screenshot-delay=") == 0) {
            try { screenshot_delay = std::max(0.0, std::stod(arg.substr(19))); } catch (...) { std::cerr << "Invalid screenshot delay\n"; return 1; }
        } else if (arg.compare(0, 7, "--size=") == 0) {
            // --size=WxH sets the window (and so the viewport) size; the default is 800x600
            std::string size = arg.substr(7);
            size_t x = size.find('x');
            try {
                window_width = std::stoi(size.substr(0, x));
                window_height = std::stoi(size.substr(x + 1));
            } catch (...) { std::cerr << "Invalid size, use --size=WxH: " << size << std::endl; return 1; }
        } else if (arg.compare(0, 14, "--layout-dump=") == 0) {
            layout_dump_path = arg.substr(14);
        } else if (arg.compare(0, 6, "--log=") == 0) {
            if (!log_open(arg.substr(6))) {
                std::cerr << "Could not open the log: " << arg.substr(6) << std::endl;
                return 1;
            }
        } else if (arg.compare(0, 11, "--user-css=") == 0) {
            user_css_path = arg.substr(11);
        } else if (arg == "--static") {
            static_mode = true;
        } else if (arg.compare(0, 13, "--screenshot=") == 0) {
            screenshot_path = arg.substr(13);
            static_mode = true;
        } else if (arg == "--debug") {
            debug_mode = DebugType::Element;
        } else if (arg == "--debug=element") {
            debug_mode = DebugType::Element;
        } else if (arg == "--debug=text") {
            debug_mode = DebugType::Text;
        } else if (arg == "--debug=dimens") {
            debug_mode = DebugType::Dimens;
        } else if (arg == "--debug=anim") {
            debug_mode = DebugType::Anim;
        } else if (!file_specified) {
            html_file = arg;
            file_specified = true;
            if (!file_exists(html_file)) {
                std::string alt_path = "assets/pages/" + html_file;
                if (file_exists(alt_path)) {
                    html_file = alt_path;
                } else {
                    std::string exe_relative = get_executable_dir() + "/" + html_file;
                    if (file_exists(exe_relative)) {
                        html_file = exe_relative;
                    }
                }
            }
        }
    }

    log_event("start", "page " + html_file);

    // 1. Initialize Window
#if !defined(WESTON_PLATFORM) && !defined(XORG_PLATFORM)
    if (!screenshot_path.empty()) {
        if (!glfwInit()) return 1;
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    }
#endif
    Window window;
    if (!window.init(window_width, window_height, "Linweb")) {
        return -1;
    }
    window.enable_blend();

    // 2. Initialize Renderer
    Renderer::init();
    Renderer::set_debug_mode(debug_mode);

    // 3. Setup Input Handlers
    KeyboardHandler keyboard_handler;
    keyboard_handler.setup(&window);
    ScrollHandler scroll_handler;
    scroll_handler.setup(&window);
    MouseHandler mouse_handler;
    mouse_handler.setup(&window);

    // 4. Load HTML
    auto start_time = std::chrono::high_resolution_clock::now();

    if (is_http_url(html_file)) {
        // a URL is downloaded with its files, and the local copy of the page is what gets loaded
        DownloadSummary summary = download_page(html_file, download_directory_for(html_file));
        log_event("download", html_file + " -> " + summary.html_path + " (" + std::to_string(summary.downloaded) +
                  " files, " + std::to_string(summary.failed) + " failed)");
        if (summary.html_path.empty()) {
            std::cerr << "Could not download " << html_file << std::endl;
            return -1;
        }
        html_file = summary.html_path;
    }

    std::string html_base_dir = get_directory(html_file);

    std::string html = read_file(html_file);
    if (html.empty()) {
        std::cerr << "Could not load " << html_file << std::endl;
        return -1;
    }

    // 5. Parse HTML
    auto html_start = std::chrono::high_resolution_clock::now();
    auto dom = HTMLParser::parse(html);
    g_dom_root = dom;
    log_event("parse", std::to_string(html.size()) + " bytes, " + std::to_string(count_dom_nodes(dom)) + " nodes");
    auto html_end = std::chrono::high_resolution_clock::now();
    
    // Resolve local downloaded resources and expand CSS imports in document order.
    auto resources_start = std::chrono::high_resolution_clock::now();
    std::string css = "body { color: black; }" + prepare_page_resources(dom, html_base_dir);
    if (!user_css_path.empty()) {
        // the user's stylesheet comes last, so it wins over the page's own rules
        std::string user_css = read_file(user_css_path);
        if (user_css.empty()) std::cerr << "Could not read the user stylesheet: " << user_css_path << std::endl;
        css += "\n" + user_css;
        log_event("css", "user stylesheet " + user_css_path + " (" + std::to_string(user_css.size()) + " bytes)");
    }
    std::vector<std::shared_ptr<ElementNode>> links, style_tags;
    find_tags(dom, "link", links);
    find_tags(dom, "style", style_tags);
    auto resources_end = std::chrono::high_resolution_clock::now();

    auto css_start = std::chrono::high_resolution_clock::now();
    int initial_width = 800, initial_height = 600;
    window.get_framebuffer_size(initial_width, initial_height);
    auto stylesheet = CSSParser::parse(css, static_cast<float>(initial_width), static_cast<float>(initial_height));
    auto css_end = std::chrono::high_resolution_clock::now();
    log_event("css", "parsed " + std::to_string(stylesheet.rules.size()) + " rules");

    // 7. Setup viewport
    int fbw = 0, fbh = 0;
    window.get_framebuffer_size(fbw, fbh);
    Renderer::set_viewport(fbw, fbh);

    Dimensions viewport;
    viewport.content = {0, 0, static_cast<float>(fbw), static_cast<float>(fbh)};

    // 8. Style & Layout
    auto style_start = std::chrono::high_resolution_clock::now();
    auto style_tree = StyleEngine::build_style_tree(dom, stylesheet, nullptr, nullptr, nullptr, get_platform_time(), viewport.content.width, viewport.content.height);
    auto style_end = std::chrono::high_resolution_clock::now();

    g_style_tree = style_tree;
    log_event("style", "styled tree built");

    auto layout_start = std::chrono::high_resolution_clock::now();
    auto layout_tree = LayoutEngine::build_layout_tree(style_tree);
    log_event("layout-tree", "boxes built");

    if (layout_tree) {
        apply_scroll_states(layout_tree);
        log_event("layout-tree", "running layout");
        layout_tree->layout(viewport);
        log_event("layout-tree", "layout done");
    }
    auto layout_end = std::chrono::high_resolution_clock::now();

    g_layout_tree = layout_tree;
    log_event("layout", std::to_string(stylesheet.rules.size()) + " css rules");

    // 9. Print Information
    auto end_time = std::chrono::high_resolution_clock::now();
    auto total_duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count() / 1000.0;

    std::cout << "\n" << std::string(40, '=') << "\n";
    std::cout << "  LINWEB PERFORMANCE & INFO\n";
    std::cout << std::string(40, '=') << "\n";

    auto print_step = [](const std::string& name, auto start, auto end, size_t info_val, const std::string& info_label) {
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() / 1000.0;
        std::cout << std::left << std::setw(20) << name << ": "
                  << std::right << std::setw(8) << std::fixed << std::setprecision(3) << duration << " ms | "
                  << info_val << " " << info_label << "\n";
    };

    print_step("HTML Parsing", html_start, html_end, count_dom_nodes(dom), "nodes");
    print_step("Resources Loading", resources_start, resources_end, links.size() + style_tags.size(), "styles");
    print_step("CSS Parsing", css_start, css_end, stylesheet.rules.size(), "rules");
    print_step("Style Tree", style_start, style_end, count_dom_nodes(style_tree->node), "styled nodes");
    print_step("Layout Tree", layout_start, layout_end, count_dom_nodes(layout_tree->styled_node->node), "boxes");

    std::cout << std::string(40, '-') << "\n";
    std::cout << std::left << std::setw(20) << "Total Pipeline" << ": "
              << std::right << std::setw(8) << total_duration << " ms\n";
    std::cout << std::string(40, '=') << "\n\n";

    // 10. JS Engine
    JSEngine js;
    g_js_engine = &js;
    keyboard_handler.set_js_engine(g_js_engine);

    // form controls: the focused node is the one :focus reads, text is measured with the renderer's font
    forms::bind_focus(&g_focused_node);
    forms::bind_dirty(&g_dom_dirty);
    forms::set_root(dom);
    forms::set_measure([](const std::string& text, float font_size) { return Renderer::measure_text(text, font_size); });
    forms::set_visibility([](const std::shared_ptr<Node>& node) {
        return g_layout_tree && find_box_for_node(g_layout_tree, node) != nullptr;
    });
    forms::install_events(g_js_engine);

    js.add_function("print", js_console_log);
    js.add_function("__setStyleProperty", js_set_style_property);
    js.add_function("__setStylePropertyById", js_set_style_property_by_id);
    js.add_function("__setStylePropertyByAddr", js_set_style_property_by_addr);
    js.add_function("__querySelectorAll", js_query_selector_all);
    js.add_function("__getElementById", js_get_element_by_id);
    js.add_function("__getValue", forms::js_get_value);
    js.add_function("__setValue", forms::js_set_value);
    js.add_function("__setAttribute", forms::js_set_attribute);
    js.add_function("__removeAttribute", forms::js_remove_attribute);
    js.add_function("__hasAttribute", forms::js_has_attribute);
    js.add_function("__focusNode", forms::js_focus_node);
    js.add_function("__openPicker", forms::js_open_picker);
    js.add_function("__blurNode", forms::js_blur_node);
    js.add_function("__getActiveElement", forms::js_get_active_element);
    js.add_function("__preventDefault", forms::js_prevent_default);
    js.add_function("__createElement", js_create_element);
    js.add_function("__appendChild", js_append_child);
    js.add_function("__setTextContent", js_set_text_content);
    js.add_function("__getTextContent", js_get_text_content);
    js.add_function("__setInnerHTML", js_set_inner_html);
    js.add_function("__getInnerHTML", js_get_inner_html);
    js.add_function("__getClassName", js_get_class_name);
    js.add_function("__setClassName", js_set_class_name);
    js.add_function("__addClassByAddr", js_add_class_by_addr);
    js.add_function("__removeClassByAddr", js_remove_class_by_addr);
    js.add_function("__toggleClassByAddr", js_toggle_class_by_addr);
    js.add_function("__getAttribute", js_get_attribute);
    js.add_function("__querySelectorAllByAddr", js_query_selector_all_by_addr);

    // Inject browser-like API
    js.execute(
        "const console = {"
        "  log: (...args) => print(...args),"
        "  error: (...args) => print('ERROR:', ...args),"
        "  warn: (...args) => print('WARN:', ...args),"
        "  info: (...args) => print('INFO:', ...args)"
        "};"
        "const window = {"
        "  _listeners: {},"
        "  _raf_callbacks: [],"
        "  _timeouts: [],"
        "  dispatchEvent: (targetId, type, data) => {"
        "    const key = targetId + ':' + type;"
        "    if (window._listeners[key]) {"
        "      window._listeners[key].forEach(cb => cb(data));"
        "    }"
        "  },"
        "  addEventListener: (type, callback) => {"
        "    const key = 'window:' + type;"
        "    if (!window._listeners[key]) window._listeners[key] = [];"
        "    window._listeners[key].push(function(evt) { return callback.call(window, evt); });"
        "  },"
        "  requestAnimationFrame: (callback) => {"
        "    window._raf_callbacks.push(callback);"
        "  },"
        "  setTimeout: (callback, delay) => {"
        "    window._timeouts.push({ cb: callback, time: Date.now() + (delay || 0) });"
        "  },"
        "  _run_raf: () => {"
        "    const callbacks = window._raf_callbacks;"
        "    window._raf_callbacks = [];"
        "    callbacks.forEach(cb => cb());"
        "    const now = Date.now();"
        "    window._timeouts = window._timeouts.filter(t => {"
        "      if (now >= t.time) { t.cb(); return false; }"
        "      return true;"
        "    });"
        "  }"
        "};"
        "const requestAnimationFrame = window.requestAnimationFrame;"
        "const setTimeout = window.setTimeout;"
        "const document = {"
        "  _wrap: (node) => ({"
        "    _addr: node._addr,"
        "    tagName: node.tagName,"
        "    getAttribute: (name) => __getAttribute(node._addr, name),"
        "    setAttribute: (name, val) => __setAttribute(node._addr, name, String(val)),"
        "    removeAttribute: (name) => __removeAttribute(node._addr, name),"
        "    hasAttribute: (name) => __hasAttribute(node._addr, name),"
        "    get value() { return __getValue(node._addr); },"
        "    set value(val) { __setValue(node._addr, String(val)); },"
        "    get valueAsNumber() { return parseFloat(__getValue(node._addr)); },"
        "    get disabled() { return __hasAttribute(node._addr, 'disabled'); },"
        "    set disabled(val) { if (val) __setAttribute(node._addr, 'disabled', ''); else __removeAttribute(node._addr, 'disabled'); },"
        "    get min() { return __getAttribute(node._addr, 'min') || ''; },"
        "    set min(val) { __setAttribute(node._addr, 'min', String(val)); },"
        "    get max() { return __getAttribute(node._addr, 'max') || ''; },"
        "    set max(val) { __setAttribute(node._addr, 'max', String(val)); },"
        "    get step() { return __getAttribute(node._addr, 'step') || ''; },"
        "    set step(val) { __setAttribute(node._addr, 'step', String(val)); },"
        "    get placeholder() { return __getAttribute(node._addr, 'placeholder') || ''; },"
        "    set placeholder(val) { __setAttribute(node._addr, 'placeholder', String(val)); },"
        "    focus: () => __focusNode(node._addr),"
        "    showPicker: () => __openPicker(node._addr),"
        "    blur: () => __blurNode(),"
        "    get className() { return __getClassName(node._addr); },"
        "    set className(val) { __setClassName(node._addr, val); },"
        "    get textContent() { return __getTextContent(node._addr); },"
        "    set textContent(val) { __setTextContent(node._addr, val); },"
        "    get innerText() { return __getTextContent(node._addr); },"
        "    set innerText(val) { __setTextContent(node._addr, val); },"
        "    get innerHTML() { return __getInnerHTML(node._addr); },"
        "    set innerHTML(val) { __setInnerHTML(node._addr, val); },"
        "    style: {"
        "      set backgroundColor(val) { __setStylePropertyByAddr(node._addr, 'background-color', val); },"
        "      set color(val) { __setStylePropertyByAddr(node._addr, 'color', val); },"
        "      set display(val) { __setStylePropertyByAddr(node._addr, 'display', val); },"
        "      set opacity(val) { __setStylePropertyByAddr(node._addr, 'opacity', val); },"
        "      set transform(val) { __setStylePropertyByAddr(node._addr, 'transform', val); }"
        "    },"
        "    classList: {"
        "      add: (...args) => __addClassByAddr(node._addr, ...args),"
        "      remove: (...args) => __removeClassByAddr(node._addr, ...args),"
        "      toggle: (cls, force) => __toggleClassByAddr(node._addr, cls, force)"
        "    },"
        "    addEventListener: (type, callback) => {"
        "      const key = node._addr + ':' + type;"
        "      if (!window._listeners[key]) window._listeners[key] = [];"
        "      const self = document._wrap(node);"
        "      window._listeners[key].push(function(evt) { return callback.call(self, evt); });"
        "    },"
        "    appendChild: (child) => {"
        "      __appendChild(node._addr, child._addr);"
        "      return child;"
        "    },"
        "    querySelectorAll: (selector) => {"
        "      return __querySelectorAllByAddr(node._addr, selector).map(n => document._wrap(n));"
        "    },"
        "    querySelector: (selector) => {"
        "      const all = __querySelectorAllByAddr(node._addr, selector).map(n => document._wrap(n));"
        "      return all.length > 0 ? all[0] : null;"
        "    }"
        "  }),"
        "  createElement: (tagName) => {"
        "    const node = __createElement(tagName);"
        "    return document._wrap(node);"
        "  },"
        "  get activeElement() {"
        "    const node = __getActiveElement();"
        "    return node ? document._wrap(node) : null;"
        "  },"
        "  getElementById: (id) => {"
        "    const node = __getElementById(id);"
        "    return node ? document._wrap(node) : null;"
        "  },"
        "  getElementsByClassName: (name) => {"
        "    return __querySelectorAll('.' + name).map(n => document._wrap(n));"
        "  },"
        "  querySelectorAll: (selector) => {"
        "    return __querySelectorAll(selector).map(n => document._wrap(n));"
        "  },"
        "  querySelector: (selector) => {"
        "    const all = document.querySelectorAll(selector);"
        "    return all.length > 0 ? all[0] : null;"
        "  },"
        "  addEventListener: (type, callback) => {"
        "    const key = 'document:' + type;"
        "    if (!window._listeners[key]) window._listeners[key] = [];"
        "    window._listeners[key].push(function(evt) { return callback.call(document, evt); });"
        "  },"
        "  get body() { return document.querySelector('body'); },"
        "  get documentElement() { return document.querySelector('html'); }"
        "};"
    );

    // Load scripts
    std::vector<std::shared_ptr<ElementNode>> scripts;
    find_tags(dom, "script", scripts);
    if (!static_mode) for (auto& script : scripts) {
        if (script->data.attributes.count("src")) {
            std::string js_path = resolve_resource_path(html_base_dir, script->data.attributes.at("src"));
            std::string js_code = read_file(js_path);
            if (!js_code.empty()) js.execute(js_code);
        } else {
            std::string js_code;
            for (const auto& child : script->children) {
                if (child->type() == NodeType::Text) {
                    js_code += std::static_pointer_cast<TextNode>(child)->data;
                }
            }
            if (!js_code.empty()) js.execute(js_code);
        }
    }

    if (!static_mode) js.execute("window.dispatchEvent('document', 'DOMContentLoaded', {});");

    // 11. Main Loop
    int last_fbw = fbw;
    int last_fbh = fbh;
    std::shared_ptr<Node> prev_hovered_dom_node;
    const double screenshot_start = get_platform_time();

    while (!window.should_close()) {
        // Handle Resize
        window.get_framebuffer_size(fbw, fbh);
        bool viewport_changed = (fbw != last_fbw || fbh != last_fbh);
        if (viewport_changed) {
            viewport.content = {0, 0, static_cast<float>(fbw), static_cast<float>(fbh)};
            Renderer::set_viewport(fbw, fbh);
            stylesheet = CSSParser::parse(css, static_cast<float>(fbw), static_cast<float>(fbh));
            last_fbw = fbw;
            last_fbh = fbh;
        }

        // Handle Hover & Click
        mouse_handler.handle_hover_and_click(&window, g_js_engine);
        auto& mouse_state = mouse_handler.get_state();
        auto current_hovered_dom_node = mouse_state.current_hovered_dom_node;

        if (!static_mode) js.execute("if (window._run_raf) window._run_raf();");

        bool input_changed = (prev_hovered_dom_node != current_hovered_dom_node) || mouse_state.had_click_event;
        bool layout_affecting_animations = StyleEngine::has_layout_affecting_animations();
        bool needs_full_rebuild = viewport_changed || input_changed || layout_affecting_animations || g_dom_dirty || !g_style_tree || !g_layout_tree;

        g_anim_debug_entries.clear();

        if (needs_full_rebuild) {
            g_dom_dirty = false;
            g_style_tree = StyleEngine::build_style_tree(dom, stylesheet, current_hovered_dom_node, g_focused_node, g_active_node, get_platform_time(), viewport.content.width, viewport.content.height);
            g_layout_tree = LayoutEngine::build_layout_tree(g_style_tree);
            if (g_layout_tree) {
                apply_scroll_states(g_layout_tree);
                g_layout_tree->layout(viewport);
            }
        } else {
            update_animations_on_tree(g_style_tree, stylesheet, get_platform_time(), viewport.content.width, viewport.content.height);
        }
        prev_hovered_dom_node = current_hovered_dom_node;

        // Render
        glClearColor(1.0f, 1.0f, 1.0f, 1.0f);

        if (g_layout_tree) {
            if (g_debug_mode_active) {
                if (g_show_node_tree) {
                    float temp_y = 30.0f;
                    g_debug_hovered_node = nullptr;
                    if (debug_mode == DebugType::Dimens) {
                        update_dimens_tree_hover(g_layout_tree, temp_y, 0, mouse_state.px, mouse_state.py, g_debug_hovered_node);
                    } else {
                        update_node_tree_hover(g_layout_tree, temp_y, 0, mouse_state.px, mouse_state.py, g_debug_hovered_node);
                    }
                }

                auto box = find_box_for_node(g_layout_tree, g_debug_hovered_node);
                Renderer::set_debug_box(box);
            }

            Renderer::render(g_layout_tree);

            if (debug_mode == DebugType::Anim && !g_anim_debug_entries.empty()) {
                float panel_x = 10.0f, panel_y = 10.0f;
                float line_h = 18.0f;
                float panel_w = 800.0f;
                int total_lines = 1;
                for (const auto& entry : g_anim_debug_entries) {
                    total_lines += 3;
                    total_lines += (int)entry.animated_props.size();
                }
                float panel_h = total_lines * line_h + 10.0f;
                Renderer::draw_rect(panel_x, panel_y, panel_w, panel_h, 0.0f, 0.0f, 0.0f, 0.75f, 6.0f);

                float text_x = panel_x + 8.0f;
                float text_y = panel_y + 6.0f;
                Renderer::draw_text("=== ANIMATIONS (" + std::to_string(g_anim_debug_entries.size()) + " active) ===", text_x, text_y, 14.0f, 0.3f, 0.8f, 1.0f, 1.0f, true);
                text_y += line_h;

                for (const auto& entry : g_anim_debug_entries) {
                    std::string node_label = "<" + entry.node_tag + ">";
                    if (!entry.node_id.empty()) node_label += " #" + entry.node_id;

                    float pct = entry.t * 100.0f;
                    float eased_pct = entry.eased_t * 100.0f;
                    char time_buf[64];
                    snprintf(time_buf, sizeof(time_buf), "%.2fs", entry.elapsed);

                    Renderer::draw_text(node_label, text_x, text_y, 14.0f, 1.0f, 1.0f, 1.0f, 1.0f, true);
                    text_y += line_h;

                    std::string progress = std::string("  [") + entry.anim_name + "] " + time_buf + "  linear: " +
                        std::to_string((int)pct) + "%  eased: " + std::to_string((int)eased_pct) + "%  (" + entry.timing_function + ")";
                    Renderer::draw_text(progress, text_x, text_y, 13.0f, 0.6f, 0.9f, 0.6f, 1.0f);
                    text_y += line_h;

                    for (const auto& [prop, val] : entry.animated_props) {
                        std::string prop_line = "    " + prop + ": " + val.actual + "  (start: " + val.start + ", end: " + val.end + ")";
                        Renderer::draw_text(prop_line, text_x, text_y, 13.0f, 0.8f, 0.8f, 0.8f, 1.0f, false, 0.0f, 0.0f, false);
                        text_y += line_h;
                    }
                }
            }

            if (g_show_node_tree) {
                float y_offset = 30.0f;
                if (debug_mode == DebugType::Dimens) {
                    render_dimens_tree(g_layout_tree, y_offset, 0, mouse_state.px, mouse_state.py);
                } else {
                    render_node_tree(g_layout_tree, y_offset, 0, mouse_state.px, mouse_state.py);
                }
            }
        }

        if (!screenshot_path.empty() && get_platform_time() - screenshot_start >= screenshot_delay) {
            if (!layout_dump_path.empty() && g_layout_tree && !dump_layout(layout_dump_path, *g_layout_tree)) return 1;
            if (!capture_frame(screenshot_path, fbw, fbh)) {
                std::cerr << "Could not save screenshot: " << screenshot_path << std::endl;
                return 1;
            }
            std::cout << "Screenshot: " << screenshot_path << std::endl;
            log_event("screenshot", screenshot_path);
            break;
        }
        window.swap_buffers();
        window.poll_events();
    }

    return 0;
}
