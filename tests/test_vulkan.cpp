// Runs the renderer on a headless surface (e.g. Mesa lavapipe). Exits 77 (skip) without one.
#include "rottr/vulkan_renderer.h"

#include <cstdio>

static int errors = 0;
static void logger(bool isError, const char* message) {
    std::printf("%s %s\n", isError ? "E" : "I", message);
    if (isError) ++errors;
}

static VkResult headless(VkInstance instance, VkSurfaceKHR* surface) {
    auto create = reinterpret_cast<PFN_vkCreateHeadlessSurfaceEXT>(
        vkGetInstanceProcAddr(instance, "vkCreateHeadlessSurfaceEXT"));
    if (create == nullptr) return VK_ERROR_EXTENSION_NOT_PRESENT;
    VkHeadlessSurfaceCreateInfoEXT info{};
    info.sType = VK_STRUCTURE_TYPE_HEADLESS_SURFACE_CREATE_INFO_EXT;
    return create(instance, &info, nullptr, surface);
}

int main() {
    rottr::VulkanRenderer renderer(logger);
    // Two rounds cover tearing down and starting again, as happens on app pause/resume.
    for (int round = 0; round < 2; ++round) {
        if (!renderer.init(VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME, headless, 640, 360)) {
            if (round == 0) {
                std::puts("SKIP: no headless Vulkan available");
                return 77;
            }
            return 1;
        }
        for (int frame = 0; frame < 90; ++frame) {
            renderer.setStagesDone(1 + frame / 30);
            if (!renderer.drawFrame(static_cast<float>(frame) / 60.0f)) return 1;
        }
        if (renderer.presentedFrames() != 90) return 1;
        renderer.destroy();
    }
    return errors == 0 ? 0 : 1;
}
