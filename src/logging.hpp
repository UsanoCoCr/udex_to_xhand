#pragma once

#include <iostream>

// Minimal stderr logging. Stream syntax: LOG_INFO("port=" << port);
// No spdlog dep — see plan §1.1 / CLAUDE.md "no abstractions beyond what the task requires".
#define LOG_INFO(msg)  do { std::cerr << "[INFO] "  << msg << std::endl; } while (0)
#define LOG_WARN(msg)  do { std::cerr << "[WARN] "  << msg << std::endl; } while (0)
#define LOG_ERROR(msg) do { std::cerr << "[ERROR] " << msg << std::endl; } while (0)
