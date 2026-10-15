// What a canvas shader's fragment() gets (see rendering/shader.h). Put in front of the game's text.
in highp vec2 vUV;          // 0..1 across the rectangle, (0, 0) = top left
in mediump vec4 vColor;     // the ShaderRect's tint
uniform highp vec2 uSize;   // the rectangle's size in pixels
out vec4 FragColor;         // premultiplied alpha
// The game's text is highp unless it says `precision mediump float;` (rendering/shader.h).
precision highp float;
