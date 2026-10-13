// What a post shader's fragment() gets (see rendering/shader.h). Put in front of the game's text.
in vec2 vUV;                 // 0..1 across the picture, (0, 0) = bottom left (like texture coordinates)
uniform sampler2D uSource;   // the previous pass's output (the first pass: the viewport's picture)
uniform sampler2D uScene;    // the viewport's picture before any pass
uniform vec2 uTexel;         // 1 / uSource's size in pixels
uniform vec2 uOutputSize;    // this pass's output size in pixels
out vec4 FragColor;          // replaces the output pixel (no blending), premultiplied alpha
