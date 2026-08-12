# AtracGlitch interface contract

AtracGlitch uses the shared `juce-ehl-design-module` as its only EHL visual implementation.

- Render the canonical short `ehl` mark through `paintEditorChrome`; do not copy or replace it with text.
- Use the shared `ink`, `low`, `mid`, and `paper` palette with the shared JUCE LookAndFeel.
- Keep all seven codec-corruption parameters on one compact, resizable surface.
- Preserve clean operational labels. Do not add neon colour, gradients, loose glitch debris, fake hardware, or waveform branding.
- The generic parameter grid is functional content inside the EHL header and spacing system; it is not a fallback brand surface.
- Parameter identifiers, ranges, state serialization, codec processing, and latency are outside the visual contract and must remain unchanged.

Minimum editor size is `512 x 400`; default size is `640 x 520`.
