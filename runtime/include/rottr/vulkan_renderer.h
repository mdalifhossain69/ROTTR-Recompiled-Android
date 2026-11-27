// Minimal Vulkan renderer: owns the instance, device and swapchain and presents real frames.
// It draws a boot status screen. It does not translate Direct3D.
#pragma once

#include <vulkan/vulkan.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <vector>

namespace rottr {

class VulkanRenderer {
public:
    using SurfaceFactory = std::function<VkResult(VkInstance, VkSurfaceKHR*)>;
    using Logger = void (*)(bool isError, const char* message);

    static constexpr int kBootStages = 4;

    explicit VulkanRenderer(Logger logger) : log_(logger) {}
    ~VulkanRenderer() { destroy(); }
    VulkanRenderer(const VulkanRenderer&) = delete;
    VulkanRenderer& operator=(const VulkanRenderer&) = delete;

    // surfaceExtension is the platform extension needed by createSurface.
    // width/height are used only when the surface does not report its own size.
    bool init(const char* surfaceExtension, const SurfaceFactory& createSurface,
              uint32_t width, uint32_t height);
    // Returns false only on an unrecoverable error. A skipped frame returns true.
    bool drawFrame(float seconds);
    void destroy();

    // May be called from any thread: how many boot stages have really completed.
    void setStagesDone(int count) { stagesDone_.store(count); }
    uint64_t presentedFrames() const { return presented_; }

private:
    bool pickDevice();
    bool createDevice();
    bool createSwapchain();
    void destroySwapchain();
    bool recreateSwapchain();
    bool record(uint32_t imageIndex, float seconds);
    bool check(VkResult result, const char* what);
    void logf(bool isError, const char* format, ...) const __attribute__((format(printf, 3, 4)));

    Logger log_;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    uint32_t queueFamily_ = 0;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    VkExtent2D extent_ = {0, 0};
    VkExtent2D requested_ = {0, 0};
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    std::vector<VkImageView> views_;
    std::vector<VkFramebuffer> framebuffers_;
    std::vector<VkSemaphore> renderDone_;  // one per swapchain image
    VkCommandPool pool_ = VK_NULL_HANDLE;
    VkCommandBuffer commands_ = VK_NULL_HANDLE;
    VkSemaphore imageReady_ = VK_NULL_HANDLE;
    VkFence frameDone_ = VK_NULL_HANDLE;
    std::atomic<int> stagesDone_{1};
    uint64_t presented_ = 0;
};

}  // namespace rottr
