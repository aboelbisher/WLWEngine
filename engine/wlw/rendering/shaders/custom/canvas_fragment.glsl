// What a canvas shader's fragment() gets (see rendering/shader.h). Put in front of the game's text.
in vec2 vUV;          // 0..1 across the rectangle, (0, 0) = top left
in vec4 vColor;       // the ShaderRect's tint
uniform vec2 uSize;   // the rectangle's size in pixels
out vec4 FragColor;   // premultiplied alpha
