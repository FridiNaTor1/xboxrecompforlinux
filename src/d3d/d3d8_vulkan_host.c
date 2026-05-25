/*
 * Minimal SDL/Vulkan presentation host for the Linux D3D8 ABI layer.
 *
 * This is intentionally only a present scaffold: it owns a window,
 * Vulkan instance/device/swapchain, and clears the swapchain each frame.
 * Real NV2A/D3D8 rendering can be lowered into this host incrementally.
 */

#include "d3d8_vulkan_host.h"

#include <SDL.h>
#include <SDL_vulkan.h>
#include <vulkan/vulkan.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SWAPCHAIN_IMAGES 8

typedef struct VulkanHost {
    SDL_Window *window;
    VkInstance instance;
    VkPhysicalDevice physical_device;
    VkDevice device;
    VkSurfaceKHR surface;
    VkQueue graphics_queue;
    VkQueue present_queue;
    uint32_t graphics_family;
    uint32_t present_family;
    VkSwapchainKHR swapchain;
    VkFormat swapchain_format;
    VkExtent2D extent;
    uint32_t image_count;
    VkImage images[MAX_SWAPCHAIN_IMAGES];
    VkCommandPool command_pool;
    VkCommandBuffer command_buffers[MAX_SWAPCHAIN_IMAGES];
    VkSemaphore image_available;
    VkSemaphore render_finished;
    VkFence in_flight;
    int initialized;
} VulkanHost;

static VulkanHost g_vk;

static void destroy_swapchain(void)
{
    if (!g_vk.device) return;
    vkDeviceWaitIdle(g_vk.device);
    if (g_vk.command_pool && g_vk.image_count) {
        vkFreeCommandBuffers(g_vk.device, g_vk.command_pool, g_vk.image_count,
                             g_vk.command_buffers);
    }
    if (g_vk.swapchain) {
        vkDestroySwapchainKHR(g_vk.device, g_vk.swapchain, NULL);
    }
    memset(g_vk.images, 0, sizeof(g_vk.images));
    memset(g_vk.command_buffers, 0, sizeof(g_vk.command_buffers));
    g_vk.swapchain = VK_NULL_HANDLE;
    g_vk.image_count = 0;
}

static VkSurfaceFormatKHR choose_surface_format(const VkSurfaceFormatKHR *formats, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) {
        if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM &&
            formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return formats[i];
        }
    }
    return formats[0];
}

static VkPresentModeKHR choose_present_mode(VkPhysicalDevice device)
{
    uint32_t count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, g_vk.surface, &count, NULL);
    VkPresentModeKHR *modes = (VkPresentModeKHR *)calloc(count ? count : 1, sizeof(*modes));
    if (!modes) return VK_PRESENT_MODE_FIFO_KHR;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, g_vk.surface, &count, modes);
    VkPresentModeKHR chosen = VK_PRESENT_MODE_FIFO_KHR;
    for (uint32_t i = 0; i < count; i++) {
        if (modes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
            chosen = VK_PRESENT_MODE_MAILBOX_KHR;
            break;
        }
    }
    free(modes);
    return chosen;
}

static int create_swapchain(uint32_t requested_width, uint32_t requested_height)
{
    VkSurfaceCapabilitiesKHR caps;
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_vk.physical_device, g_vk.surface, &caps) != VK_SUCCESS) {
        return 0;
    }

    if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT)) {
        fprintf(stderr, "[D3D8/Vulkan] Surface does not support transfer clears\n");
        return 0;
    }

    uint32_t format_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(g_vk.physical_device, g_vk.surface, &format_count, NULL);
    if (!format_count) return 0;

    VkSurfaceFormatKHR *formats = (VkSurfaceFormatKHR *)calloc(format_count, sizeof(*formats));
    if (!formats) return 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(g_vk.physical_device, g_vk.surface, &format_count, formats);
    VkSurfaceFormatKHR surface_format = choose_surface_format(formats, format_count);
    free(formats);

    VkExtent2D extent;
    if (caps.currentExtent.width != UINT32_MAX) {
        extent = caps.currentExtent;
    } else {
        extent.width = requested_width ? requested_width : 640;
        extent.height = requested_height ? requested_height : 480;
        if (extent.width < caps.minImageExtent.width) extent.width = caps.minImageExtent.width;
        if (extent.height < caps.minImageExtent.height) extent.height = caps.minImageExtent.height;
        if (extent.width > caps.maxImageExtent.width) extent.width = caps.maxImageExtent.width;
        if (extent.height > caps.maxImageExtent.height) extent.height = caps.maxImageExtent.height;
    }

    uint32_t image_count = caps.minImageCount + 1;
    if (caps.maxImageCount && image_count > caps.maxImageCount) image_count = caps.maxImageCount;
    if (image_count > MAX_SWAPCHAIN_IMAGES) image_count = MAX_SWAPCHAIN_IMAGES;

    uint32_t queue_indices[2] = { g_vk.graphics_family, g_vk.present_family };
    VkSwapchainCreateInfoKHR info;
    memset(&info, 0, sizeof(info));
    info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    info.surface = g_vk.surface;
    info.minImageCount = image_count;
    info.imageFormat = surface_format.format;
    info.imageColorSpace = surface_format.colorSpace;
    info.imageExtent = extent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (g_vk.graphics_family != g_vk.present_family) {
        info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        info.queueFamilyIndexCount = 2;
        info.pQueueFamilyIndices = queue_indices;
    } else {
        info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    info.preTransform = caps.currentTransform;
    info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    info.presentMode = choose_present_mode(g_vk.physical_device);
    info.clipped = VK_TRUE;

    if (vkCreateSwapchainKHR(g_vk.device, &info, NULL, &g_vk.swapchain) != VK_SUCCESS) {
        return 0;
    }

    g_vk.swapchain_format = surface_format.format;
    g_vk.extent = extent;
    vkGetSwapchainImagesKHR(g_vk.device, g_vk.swapchain, &g_vk.image_count, NULL);
    if (g_vk.image_count > MAX_SWAPCHAIN_IMAGES) g_vk.image_count = MAX_SWAPCHAIN_IMAGES;
    vkGetSwapchainImagesKHR(g_vk.device, g_vk.swapchain, &g_vk.image_count, g_vk.images);

    VkCommandBufferAllocateInfo alloc;
    memset(&alloc, 0, sizeof(alloc));
    alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc.commandPool = g_vk.command_pool;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = g_vk.image_count;
    return vkAllocateCommandBuffers(g_vk.device, &alloc, g_vk.command_buffers) == VK_SUCCESS;
}

static int pick_device(void)
{
    uint32_t device_count = 0;
    vkEnumeratePhysicalDevices(g_vk.instance, &device_count, NULL);
    if (!device_count) return 0;

    VkPhysicalDevice *devices = (VkPhysicalDevice *)calloc(device_count, sizeof(*devices));
    if (!devices) return 0;
    vkEnumeratePhysicalDevices(g_vk.instance, &device_count, devices);

    for (uint32_t d = 0; d < device_count; d++) {
        uint32_t family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(devices[d], &family_count, NULL);
        VkQueueFamilyProperties *families =
            (VkQueueFamilyProperties *)calloc(family_count, sizeof(*families));
        if (!families) continue;
        vkGetPhysicalDeviceQueueFamilyProperties(devices[d], &family_count, families);

        uint32_t graphics = UINT32_MAX;
        uint32_t present = UINT32_MAX;
        for (uint32_t i = 0; i < family_count; i++) {
            if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) graphics = i;
            VkBool32 supports_present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(devices[d], i, g_vk.surface, &supports_present);
            if (supports_present) present = i;
        }
        free(families);

        if (graphics != UINT32_MAX && present != UINT32_MAX) {
            g_vk.physical_device = devices[d];
            g_vk.graphics_family = graphics;
            g_vk.present_family = present;
            free(devices);
            return 1;
        }
    }

    free(devices);
    return 0;
}

static int create_device(void)
{
    float priority = 1.0f;
    VkDeviceQueueCreateInfo queues[2];
    memset(queues, 0, sizeof(queues));
    queues[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queues[0].queueFamilyIndex = g_vk.graphics_family;
    queues[0].queueCount = 1;
    queues[0].pQueuePriorities = &priority;
    uint32_t queue_count = 1;

    if (g_vk.present_family != g_vk.graphics_family) {
        queues[1].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queues[1].queueFamilyIndex = g_vk.present_family;
        queues[1].queueCount = 1;
        queues[1].pQueuePriorities = &priority;
        queue_count = 2;
    }

    const char *extensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkDeviceCreateInfo info;
    memset(&info, 0, sizeof(info));
    info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    info.queueCreateInfoCount = queue_count;
    info.pQueueCreateInfos = queues;
    info.enabledExtensionCount = 1;
    info.ppEnabledExtensionNames = extensions;

    if (vkCreateDevice(g_vk.physical_device, &info, NULL, &g_vk.device) != VK_SUCCESS) {
        return 0;
    }

    vkGetDeviceQueue(g_vk.device, g_vk.graphics_family, 0, &g_vk.graphics_queue);
    vkGetDeviceQueue(g_vk.device, g_vk.present_family, 0, &g_vk.present_queue);

    VkCommandPoolCreateInfo pool;
    memset(&pool, 0, sizeof(pool));
    pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool.queueFamilyIndex = g_vk.graphics_family;
    if (vkCreateCommandPool(g_vk.device, &pool, NULL, &g_vk.command_pool) != VK_SUCCESS) {
        return 0;
    }

    VkSemaphoreCreateInfo sem;
    memset(&sem, 0, sizeof(sem));
    sem.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fence;
    memset(&fence, 0, sizeof(fence));
    fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    return vkCreateSemaphore(g_vk.device, &sem, NULL, &g_vk.image_available) == VK_SUCCESS &&
           vkCreateSemaphore(g_vk.device, &sem, NULL, &g_vk.render_finished) == VK_SUCCESS &&
           vkCreateFence(g_vk.device, &fence, NULL, &g_vk.in_flight) == VK_SUCCESS;
}

static void record_clear(uint32_t image_index)
{
    VkCommandBuffer cmd = g_vk.command_buffers[image_index];

    VkCommandBufferBeginInfo begin;
    memset(&begin, 0, sizeof(begin));
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);

    VkImageMemoryBarrier to_clear;
    memset(&to_clear, 0, sizeof(to_clear));
    to_clear.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    to_clear.srcAccessMask = 0;
    to_clear.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    to_clear.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    to_clear.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_clear.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_clear.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_clear.image = g_vk.images[image_index];
    to_clear.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    to_clear.subresourceRange.levelCount = 1;
    to_clear.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                         0, NULL, 0, NULL, 1, &to_clear);

    VkClearColorValue color = { { 0.02f, 0.03f, 0.04f, 1.0f } };
    VkImageSubresourceRange range;
    memset(&range, 0, sizeof(range));
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.levelCount = 1;
    range.layerCount = 1;
    vkCmdClearColorImage(cmd, g_vk.images[image_index],
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         &color, 1, &range);

    VkImageMemoryBarrier to_present = to_clear;
    to_present.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    to_present.dstAccessMask = 0;
    to_present.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_present.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0,
                         0, NULL, 0, NULL, 1, &to_present);

    vkEndCommandBuffer(cmd);
}

int d3d8_vulkan_host_init(uint32_t width, uint32_t height)
{
    if (g_vk.initialized) return 1;
    memset(&g_vk, 0, sizeof(g_vk));

    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "[D3D8/Vulkan] SDL video init failed: %s\n", SDL_GetError());
        return 0;
    }

    width = width ? width : 640;
    height = height ? height : 480;
    g_vk.window = SDL_CreateWindow("xboxrecomp",
                                   SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                   (int)width, (int)height,
                                   SDL_WINDOW_VULKAN | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!g_vk.window) {
        fprintf(stderr, "[D3D8/Vulkan] SDL window creation failed: %s\n", SDL_GetError());
        return 0;
    }

    unsigned ext_count = 0;
    if (!SDL_Vulkan_GetInstanceExtensions(g_vk.window, &ext_count, NULL)) {
        fprintf(stderr, "[D3D8/Vulkan] Could not query SDL Vulkan extensions: %s\n", SDL_GetError());
        return 0;
    }
    const char **extensions = (const char **)calloc(ext_count, sizeof(*extensions));
    if (!extensions) return 0;
    SDL_Vulkan_GetInstanceExtensions(g_vk.window, &ext_count, extensions);

    VkApplicationInfo app;
    memset(&app, 0, sizeof(app));
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "xboxrecomp";
    app.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo instance_info;
    memset(&instance_info, 0, sizeof(instance_info));
    instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_info.pApplicationInfo = &app;
    instance_info.enabledExtensionCount = ext_count;
    instance_info.ppEnabledExtensionNames = extensions;

    VkResult result = vkCreateInstance(&instance_info, NULL, &g_vk.instance);
    free(extensions);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "[D3D8/Vulkan] vkCreateInstance failed (%d)\n", result);
        return 0;
    }

    if (!SDL_Vulkan_CreateSurface(g_vk.window, g_vk.instance, &g_vk.surface)) {
        fprintf(stderr, "[D3D8/Vulkan] SDL_Vulkan_CreateSurface failed: %s\n", SDL_GetError());
        return 0;
    }

    if (!pick_device() || !create_device() || !create_swapchain(width, height)) {
        fprintf(stderr, "[D3D8/Vulkan] Vulkan device/swapchain creation failed\n");
        return 0;
    }

    g_vk.initialized = 1;
    fprintf(stderr, "[D3D8/Vulkan] Presentation host initialized (%ux%u)\n",
            g_vk.extent.width, g_vk.extent.height);
    return 1;
}

void d3d8_vulkan_host_present(void)
{
    if (!g_vk.initialized) return;

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            d3d8_vulkan_host_shutdown();
            return;
        }
    }

    vkWaitForFences(g_vk.device, 1, &g_vk.in_flight, VK_TRUE, UINT64_MAX);
    vkResetFences(g_vk.device, 1, &g_vk.in_flight);

    uint32_t image_index = 0;
    VkResult result = vkAcquireNextImageKHR(g_vk.device, g_vk.swapchain, UINT64_MAX,
                                            g_vk.image_available, VK_NULL_HANDLE,
                                            &image_index);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) return;
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) return;

    vkResetCommandBuffer(g_vk.command_buffers[image_index], 0);
    record_clear(image_index);

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo submit;
    memset(&submit, 0, sizeof(submit));
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &g_vk.image_available;
    submit.pWaitDstStageMask = &wait_stage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &g_vk.command_buffers[image_index];
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &g_vk.render_finished;

    if (vkQueueSubmit(g_vk.graphics_queue, 1, &submit, g_vk.in_flight) != VK_SUCCESS) {
        return;
    }

    VkPresentInfoKHR present;
    memset(&present, 0, sizeof(present));
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &g_vk.render_finished;
    present.swapchainCount = 1;
    present.pSwapchains = &g_vk.swapchain;
    present.pImageIndices = &image_index;
    vkQueuePresentKHR(g_vk.present_queue, &present);
}

void d3d8_vulkan_host_shutdown(void)
{
    if (!g_vk.instance && !g_vk.window) return;

    if (g_vk.device) vkDeviceWaitIdle(g_vk.device);
    destroy_swapchain();
    if (g_vk.device && g_vk.in_flight) vkDestroyFence(g_vk.device, g_vk.in_flight, NULL);
    if (g_vk.device && g_vk.render_finished) vkDestroySemaphore(g_vk.device, g_vk.render_finished, NULL);
    if (g_vk.device && g_vk.image_available) vkDestroySemaphore(g_vk.device, g_vk.image_available, NULL);
    if (g_vk.device && g_vk.command_pool) vkDestroyCommandPool(g_vk.device, g_vk.command_pool, NULL);
    if (g_vk.device) vkDestroyDevice(g_vk.device, NULL);
    if (g_vk.instance && g_vk.surface) vkDestroySurfaceKHR(g_vk.instance, g_vk.surface, NULL);
    if (g_vk.instance) vkDestroyInstance(g_vk.instance, NULL);
    if (g_vk.window) SDL_DestroyWindow(g_vk.window);
    memset(&g_vk, 0, sizeof(g_vk));
}
