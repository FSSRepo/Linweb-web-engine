# Linweb - Lightweight Web Rendering Engine

A lightweight HTML/CSS/JS rendering engine built from scratch with C++17 and OpenGL.

## Pipeline

1. **HTML Parsing** - Tokenizer, element parser, text parser
2. **CSS Parsing** - Tokenizer, selector parser, declaration parser, keyframe parser
3. **Style Engine** - Selector matching, property resolution, transitions, animations
4. **Layout Engine** - Block, inline, flex, grid, positioned layout modes
5. **OpenGL Rendering** - Primitives, text (stb_truetype), images (stb_image), SVG (nanosvg), shaders, textures

## JavaScript Integration (QuickJS)

### DOM API
- `document.getElementById(id)`
- `document.getElementsByClassName(name)`
- `document.querySelector(selector)`
- `document.querySelectorAll(selector)`
- `document.createElement(tagName)`
- `document.body`
- `document.documentElement`
- `element.textContent` / `element.innerText`
- `element.innerHTML`
- `element.className`
- `element.classList.add(...)` / `.remove(...)` / `.toggle(cls, force)`
- `element.getAttribute(name)`
- `element.appendChild(child)`
- `element.querySelector(selector)` / `element.querySelectorAll(selector)`

### Style API
- `element.style.backgroundColor`
- `element.style.color`
- `element.style.display`
- `element.style.opacity`
- `element.style.transform`

### Event API
- `element.addEventListener(type, callback)`
- `document.addEventListener(type, callback)`
- `window.addEventListener(type, callback)`
- `window.dispatchEvent(targetId, type, data)`

### Timer API
- `setTimeout(callback, delay)`
- `requestAnimationFrame(callback)`

### Console API
- `console.log(...args)`
- `console.error(...args)`
- `console.warn(...args)`
- `console.info(...args)`

### Browser-like Globals
- `document`
- `window`
- `console`

## Input Handling

- **Keyboard** - keydown/keyup events
- **Mouse** - click, hover, cursor tracking
- **Scroll** - wheel events, scrollable containers

## Debug Tools

- Node tree overlay (`--debug` or `--debug=element`)
- Text rendering debug (`--debug=text`)
- Dimension debug (`--debug=dimens`)
- Animation debug (`--debug=anim`)

## Build Instructions

### Prerequisites
- CMake 3.10+
- C++17 compatible compiler (MSVC, GCC, Clang)
- OpenGL
- One of the following backends:
  - **GLFW** (default) - `glfw3`
  - **Wayland/Weston** - `wayland-client`, `wayland-egl`, `EGL`, `GLESv2`
  - **X11/EGL** - `X11`, `EGL`, `OpenGL`

### Build
```bash
cmake -B build
cmake --build build --config Release
```

### Platform Options
```bash
# Wayland/Weston
cmake -B build -DWESTON_PLATFORM=ON

# X11/EGL
cmake -B build -DDXORG_PLATFORM=ON
```

### Run
```bash
# Default page
build\Release\linweb.exe

# Specific HTML file
build\Release\linweb.exe examples/index.html

# With debug overlay
build\Release\linweb.exe --debug examples/index.html
build\Release\linweb.exe --debug=dimens examples/index.html
build\Release\linweb.exe --debug=anim examples/index.html
```

## Project Structure

```
├── assets/
│   └── fonts/            # TrueType font files (Arial, Monospace)
├── examples/
│   ├── ps3/              # PS3 WebOS-style demo app
│   ├── tests/            # HTML/CSS feature tests
│   ├── index.html        # CERN-inspired demo page
│   ├── new_features.html # Typography, scroll, margin collapse demo
│   └── ...               # Various feature demos
├── external/
│   ├── glad/             # OpenGL loader
│   ├── nanosvg/          # SVG parser
│   ├── quickjs/          # JavaScript engine
│   └── stb/              # stb_image, stb_truetype
├── src/
│   ├── core/             # DOM tree, navigator, helpers
│   ├── debug/            # Node tree view, debug visualizer
│   ├── layout/           # Block, inline, flex, grid, positioned
│   ├── parser/
│   │   ├── css/          # CSS tokenizer & parsers
│   │   └── html/         # HTML tokenizer & parsers
│   ├── platform/         # Window abstraction (GLFW, Weston, X11)
│   ├── renderer/         # Shaders, textures, fonts, SVG, primitives
│   ├── scripting/        # JS engine, DOM/style/event/timer bindings
│   ├── style/            # Style engine, transitions, animations
│   └── utils/            # File I/O, traversal, scroll manager
├── CMakeLists.txt
└── README.md
```

## Supported HTML Features

- Elements: `div`, `span`, `p`, `h1`-`h3`, `a`, `img`, `button`, `input`, `header`, `footer`, `nav`, `section`, `main`, `aside`, `article`, `dl`, `dt`, `dd`, `ul`, `ol`, `li`, `strong`, `em`, `code`, `br`, `style`, `script`, `link`
- Inline elements with proper whitespace handling
- Default browser-like styles for tags
- External and inline CSS
- External and inline JavaScript
- Resource path resolution for images and scripts

## Supported CSS Features

- **Selectors**: element, class, ID, descendant, child, pseudo-classes (`:hover`, `:focus`, `:active`)
- **Box model**: margin, padding, border (width, color, style, radius)
- **Display**: `block`, `inline`, `inline-block`, `flex`, `grid`, `none`
- **Flexbox**: direction, wrap, justify-content, align-items, gap
- **Grid**: template columns/rows, gap, column/row spanning
- **Positioning**: `static`, `relative`, `absolute`, `fixed` (with top/left/right/bottom)
- **Typography**: font-family, font-size, font-weight, text-align, color, line-height
- **Background**: color, image
- **Transitions**: property, duration, timing-function, delay
- **Animations**: keyframes, name, duration, iteration-count, direction
- **Transforms**: translate, rotate, scale (2D)
- **CSS Filters**: blur, invert, drop-shadow
- **Overflow**: visible, hidden, scroll, auto
- **Z-index** stacking
- **Margin collapse**
- **Opacity**
- **Gradients**: linear, radial
- **Inline SVG** rendering
- **Box shadow**

## Performance

On startup, the engine prints a performance breakdown of each pipeline stage:
- HTML Parsing (ms + node count)
- Resources Loading (ms + style count)
- CSS Parsing (ms + rule count)
- Style Tree (ms + styled node count)
- Layout Tree (ms + box count)
