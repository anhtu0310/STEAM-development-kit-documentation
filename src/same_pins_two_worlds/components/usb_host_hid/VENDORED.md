# Vendored copy of espressif/usb_host_hid 1.2.0

Copied unchanged from the `managed_components/` folder of
`STEAM_BOARD_IDF54_ALL_IN_ONE_DEMO` (Apache-2.0, see LICENSE) so the project
builds without the ESP component registry. The upstream manifest is kept as
`idf_component.yml.upstream`; it only adds the `usb` dependency on IDF >= 6.0.
To use the registry instead, delete this folder and add
`espressif/usb_host_hid: "^1.0.1"` to `main/idf_component.yml`.
