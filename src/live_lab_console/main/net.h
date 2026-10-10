#pragma once

// W5500 Ethernet (DHCP, static fallback) + Wi-Fi soft-AP, one lwIP stack,
// and a TCP sink on port 5001 on both interfaces for `netload.py blast`.
// Everything here runs on core 0.
bool net_start();
