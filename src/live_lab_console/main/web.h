#pragma once

// HTTP dashboard on port 80, reachable over Ethernet and the Wi-Fi AP.
// GET /            the same console as the VGA screen, in a browser
// GET /api/status  JSON snapshot (app state + VGA refill statistics)
// POST /api/cmd?c= relay | stress | envelope | clear | mark
bool web_start();
