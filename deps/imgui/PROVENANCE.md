# Dear ImGui

Vendored from the official `ocornut/imgui` repository, tag `v1.89.4`, commit
`f3f6295d532b98e322691afb8ef418209d963786`:
https://github.com/ocornut/imgui/tree/f3f6295d532b98e322691afb8ef418209d963786

This version preserves the API used by the first GML desktop UI consumer. It
does not depend on VRify or Localify's private exports or ImGui context.
The core, headers, Win32 backend and D3D11 backend are included. License: MIT,
retained in `LICENSE.txt`.

One local backend correction: the D3D11 restore path releases geometry shader
class instances, which the original backend omitted.
GML additionally saves the output merger targets/UAVs and the hull, domain and
compute shaders in its own host wrapper, because this backend does not restore
those states. No other upstream source changes are intended.
