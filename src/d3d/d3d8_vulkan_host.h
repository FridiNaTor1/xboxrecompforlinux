#pragma once

#include <stdint.h>

int  d3d8_vulkan_host_init(uint32_t width, uint32_t height);
void d3d8_vulkan_host_present(void);
void d3d8_vulkan_host_shutdown(void);
