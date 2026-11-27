// graphics.cpp
// Direct3D 11 → Vulkan translation layer implementation
// Provides the function declarations from graphics_types.h
// Phase 6: Added pipeline cache, descriptor pooling, frame pacing

#include "graphics/graphics_types.h"
#include "video/video_types.h"
#include <cstdint>
#include <cstring>
#include <vector>
#include <chrono>
#include <mutex>
#include <thread>\n\n#if defined(__ANDROID__)\n#include <android/log.h>\n#define ROTTR_GFX_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "ROTTR-Graphics", __VA_ARGS__)\n#define ROTTR_GFX_LOGI(...) __android_log_print(ANDROID_LOG_INFO, "ROTTR-Graphics", __VA_ARGS__)\n#endif

// Platform-specific includes for Android
#if defined(__ANDROID__)
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>
#else
// Stub definitions for non-Android builds
typedef struct ANativeWindow ANativeWindow;
void ANativeWindow_acquire(ANativeWindow* window) { (void)window; }
void ANativeWindow_release(ANativeWindow* window) { (void)window; }
#define VK_API_VERSION_1_0 1
typedef void* VkInstance;
typedef void* VkPhysicalDevice;
typedef void* VkDevice;
typedef void* VkQueue;
typedef void* VkSwapchainKHR;
typedef void* VkSurfaceKHR;
typedef void* VkCommandPool;
typedef void* VkCommandBuffer;
typedef void* VkSemaphore;
typedef void* VkFence;
typedef void* VkImage;
typedef void* VkImageView;
typedef void* VkRenderPass;
typedef void* VkFramebuffer;
typedef void* VkPipelineLayout;
typedef void* VkPipeline;
typedef void* VkDescriptorSetLayout;
typedef void* VkDescriptorPool;
typedef void* VkDescriptorSet;
typedef void* VkBuffer;
typedef void* VkDeviceMemory;
typedef void* VkPipelineCache;
typedef uint64_t VkDeviceSize;
typedef uint32_t VkBufferUsageFlags;
typedef uint32_t VkMemoryPropertyFlags;
typedef int VkResult;
#define VK_SUCCESS 0
#define VK_STRUCTURE_TYPE_APPLICATION_INFO 0
#define VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO 1
#define VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO 3
#define VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO 4
#define VK_STRUCTURE_TYPE_SURFACE_CREATE_INFO 0
#define VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR 1000001000
#define VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO 0
#define VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO 0
#define VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO 0
#define VK_STRUCTURE_TYPE_FENCE_CREATE_INFO 0
#define VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO 0
#define VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO 0
#define VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO 0
#define VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO 0
#define VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO 1000000000
#define VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO 0
#define VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO 0
#define VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO 0
#define VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO 0
#define VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO 0
#define VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO 0
#define VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO 0
#define VK_STRUCTURE_TYPE_SUBMIT_INFO 0
#define VK_STRUCTURE_TYPE_PRESENT_INFO_KHR 1000001001
#define VK_QUEUE_GRAPHICS_BIT 1
#define VK_SHARING_MODE_EXCLUSIVE 0
#define VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT 1
#define VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR 1
#define VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR 1
#define VK_PRESENT_MODE_FIFO_KHR 0
#define VK_PRESENT_MODE_MAILBOX_KHR 4
#define VK_FORMAT_B8G8R8A8_UNORM 0
#define VK_COLOR_SPACE_SRGB_NONLINEAR_KHR 0
#define VK_IMAGE_ASPECT_COLOR_BIT 1
#define VK_IMAGE_VIEW_TYPE_2D 1
#define VK_COMPONENT_SWIZZLE_R 0
#define VK_COMPONENT_SWIZZLE_G 1
#define VK_COMPONENT_SWIZZLE_B 2
#define VK_COMPONENT_SWIZZLE_A 3
#define VK_IMAGE_LAYOUT_UNDEFINED 0
#define VK_IMAGE_LAYOUT_PRESENT_SRC_KHR 1000001002
#define VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL 0
#define VK_PIPELINE_BIND_POINT_GRAPHICS 0
#define VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST 0
#define VK_POLYGON_MODE_FILL 0
#define VK_CULL_MODE_BACK_BIT 1
#define VK_FRONT_FACE_CLOCKWISE 0
#define VK_SAMPLE_COUNT_1_BIT 1
#define VK_ATTACHMENT_LOAD_OP_CLEAR 1
#define VK_ATTACHMENT_STORE_OP_STORE 1
#define VK_ATTACHMENT_LOAD_OP_DONT_CARE 0
#define VK_ATTACHMENT_STORE_OP_DONT_CARE 0
#define VK_DEPENDENCY_BY_REGION_BIT 1
#define VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT 1
#define VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT 1
#define VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER 0
#define VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER 1
#define VK_SHADER_STAGE_VERTEX_BIT 1
#define VK_SHADER_STAGE_FRAGMENT_BIT 2
#define VK_NULL_HANDLE 0
#define VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT 1

// Stub Vulkan functions for non-Android builds
void vkCreatePipelineCache(VkDevice device, const void* pCreateInfo, const void* pAllocator, VkPipelineCache* pPipelineCache) { (void)device; (void)pCreateInfo; (void)pAllocator; (void)pPipelineCache; }
void vkDestroyPipelineCache(VkDevice device, VkPipelineCache pipelineCache, const void* pAllocator) { (void)device; (void)pipelineCache; (void)pAllocator; }
void vkCreateDescriptorPool(VkDevice device, const void* pCreateInfo, const void* pAllocator, VkDescriptorPool* pDescriptorPool) { (void)device; (void)pCreateInfo; (void)pAllocator; (void)pDescriptorPool; }
void vkDestroyDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool, const void* pAllocator) { (void)device; (void)descriptorPool; (void)pAllocator; }
void vkCreateDescriptorSetLayout(VkDevice device, const void* pCreateInfo, const void* pAllocator, VkDescriptorSetLayout* pSetLayout) { (void)device; (void)pCreateInfo; (void)pAllocator; (void)pSetLayout; }
void vkDestroyDescriptorSetLayout(VkDevice device, VkDescriptorSetLayout descriptorSetLayout, const void* pAllocator) { (void)device; (void)descriptorSetLayout; (void)pAllocator; }
void vkAllocateDescriptorSets(VkDevice device, const void* pAllocateInfo, VkDescriptorSet* pDescriptorSets) { (void)device; (void)pAllocateInfo; (void)pDescriptorSets; }
void vkCreateSwapchainKHR(VkDevice device, const void* pCreateInfo, const void* pAllocator, VkSwapchainKHR* pSwapchain) { (void)device; (void)pCreateInfo; (void)pAllocator; (void)pSwapchain; }
void vkDestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain, const void* pAllocator) { (void)device; (void)swapchain; (void)pAllocator; }
void vkAcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t* pImageIndex) { (void)device; (void)swapchain; (void)timeout; (void)semaphore; (void)fence; (void)pImageIndex; }
void vkQueuePresentKHR(VkQueue queue, const void* pPresentInfo) { (void)queue; (void)pPresentInfo; }
void vkQueueSubmit(VkQueue queue, uint32_t submitCount, const void* pSubmits, VkFence fence) { (void)queue; (void)submitCount; (void)pSubmits; (void)fence; }
void vkWaitForFences(VkDevice device, uint32_t fenceCount, const VkFence* pFences, uint32_t waitAll, uint64_t timeout) { (void)device; (void)fenceCount; (void)pFences; (void)waitAll; (void)timeout; }
void vkResetFences(VkDevice device, uint32_t fenceCount, const VkFence* pFences) { (void)device; (void)fenceCount; (void)pFences; }
void vkCreateSemaphore(VkDevice device, const void* pCreateInfo, const void* pAllocator, VkSemaphore* pSemaphore) { (void)device; (void)pCreateInfo; (void)pAllocator; (void)pSemaphore; }
void vkDestroySemaphore(VkDevice device, VkSemaphore semaphore, const void* pAllocator) { (void)device; (void)semaphore; (void)pAllocator; }
void vkCreateFence(VkDevice device, const void* pCreateInfo, const void* pAllocator, VkFence* pFence) { (void)device; (void)pCreateInfo; (void)pAllocator; (void)pFence; }
void vkDestroyFence(VkDevice device, VkFence fence, const void* pAllocator) { (void)device; (void)fence; (void)pAllocator; }
void vkCreateCommandPool(VkDevice device, const void* pCreateInfo, const void* pAllocator, VkCommandPool* pCommandPool) { (void)device; (void)pCreateInfo; (void)pAllocator; (void)pCommandPool; }
void vkDestroyCommandPool(VkDevice device, VkCommandPool commandPool, const void* pAllocator) { (void)device; (void)commandPool; (void)pAllocator; }
void vkAllocateCommandBuffers(VkDevice device, const void* pAllocateInfo, VkCommandBuffer* pCommandBuffers) { (void)device; (void)pAllocateInfo; (void)pCommandBuffers; }
void vkBeginCommandBuffer(VkCommandBuffer commandBuffer, const void* pBeginInfo) { (void)commandBuffer; (void)pBeginInfo; }
void vkEndCommandBuffer(VkCommandBuffer commandBuffer) { (void)commandBuffer; }
void vkCreateRenderPass(VkDevice device, const void* pCreateInfo, const void* pAllocator, VkRenderPass* pRenderPass) { (void)device; (void)pCreateInfo; (void)pAllocator; (void)pRenderPass; }
void vkDestroyRenderPass(VkDevice device, VkRenderPass renderPass, const void* pAllocator) { (void)device; (void)renderPass; (void)pAllocator; }
void vkCreateFramebuffer(VkDevice device, const void* pCreateInfo, const void* pAllocator, VkFramebuffer* pFramebuffer) { (void)device; (void)pCreateInfo; (void)pAllocator; (void)pFramebuffer; }
void vkDestroyFramebuffer(VkDevice device, VkFramebuffer framebuffer, const void* pAllocator) { (void)device; (void)framebuffer; (void)pAllocator; }
void vkCreateImageView(VkDevice device, const void* pCreateInfo, const void* pAllocator, VkImageView* pView) { (void)device; (void)pCreateInfo; (void)pAllocator; (void)pView; }
void vkDestroyImageView(VkDevice device, VkImageView imageView, const void* pAllocator) { (void)device; (void)imageView; (void)pAllocator; }
void vkCreateBuffer(VkDevice device, const void* pCreateInfo, const void* pAllocator, VkBuffer* pBuffer) { (void)device; (void)pCreateInfo; (void)pAllocator; (void)pBuffer; }
void vkDestroyBuffer(VkDevice device, VkBuffer buffer, const void* pAllocator) { (void)device; (void)buffer; (void)pAllocator; }
void vkAllocateMemory(VkDevice device, const void* pAllocateInfo, const void* pAllocator, VkDeviceMemory* pMemory) { (void)device; (void)pAllocateInfo; (void)pAllocator; (void)pMemory; }
void vkFreeMemory(VkDevice device, VkDeviceMemory memory, const void* pAllocator) { (void)device; (void)memory; (void)pAllocator; }
void vkBindBufferMemory(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize memoryOffset) { (void)device; (void)buffer; (void)memory; (void)memoryOffset; }
void vkMapMemory(VkDevice device, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size, uint32_t flags, void** ppData) { (void)device; (void)memory; (void)offset; (void)size; (void)flags; (void)ppData; }
void vkUnmapMemory(VkDevice device, VkDeviceMemory memory) { (void)device; (void)memory; }
void vkCmdBeginRenderPass(VkCommandBuffer commandBuffer, const void* pRenderPassBegin, int contents) { (void)commandBuffer; (void)pRenderPassBegin; (void)contents; }
void vkCmdEndRenderPass(VkCommandBuffer commandBuffer) { (void)commandBuffer; }
void vkCmdBindPipeline(VkCommandBuffer commandBuffer, int pipelineBindPoint, VkPipeline pipeline) { (void)commandBuffer; (void)pipelineBindPoint; (void)pipeline; }
void vkCmdBindDescriptorSets(VkCommandBuffer commandBuffer, int pipelineBindPoint, VkPipelineLayout layout, uint32_t firstSet, uint32_t descriptorSetCount, const VkDescriptorSet* pDescriptorSets, uint32_t dynamicOffsetCount, const uint32_t* pDynamicOffsets) { (void)commandBuffer; (void)pipelineBindPoint; (void)layout; (void)firstSet; (void)descriptorSetCount; (void)pDescriptorSets; (void)dynamicOffsetCount; (void)pDynamicOffsets; }
void vkCmdDraw(VkCommandBuffer commandBuffer, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) { (void)commandBuffer; (void)vertexCount; (void)instanceCount; (void)firstVertex; (void)firstInstance; }
void vkCmdDrawIndexed(VkCommandBuffer commandBuffer, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) { (void)commandBuffer; (void)indexCount; (void)instanceCount; (void)firstIndex; (void)vertexOffset; (void)firstInstance; }
void vkCreateInstance(const void* pCreateInfo, const void* pAllocator, VkInstance* pInstance) { (void)pCreateInfo; (void)pAllocator; (void)pInstance; }
void vkDestroyInstance(VkInstance instance, const void* pAllocator) { (void)instance; (void)pAllocator; }
void vkEnumeratePhysicalDevices(VkInstance instance, uint32_t* pPhysicalDeviceCount, VkPhysicalDevice* pPhysicalDevices) { (void)instance; (void)pPhysicalDeviceCount; (void)pPhysicalDevices; }
void vkGetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice physicalDevice, uint32_t* pQueueFamilyPropertyCount, void* pQueueFamilyProperties) { (void)physicalDevice; (void)pQueueFamilyPropertyCount; (void)pQueueFamilyProperties; }
void vkCreateDevice(VkPhysicalDevice physicalDevice, const void* pCreateInfo, const void* pAllocator, VkDevice* pDevice) { (void)physicalDevice; (void)pCreateInfo; (void)pAllocator; (void)pDevice; }
void vkDestroyDevice(VkDevice device, const void* pAllocator) { (void)device; (void)pAllocator; }
void vkGetDeviceQueue(VkDevice device, uint32_t queueFamilyIndex, uint32_t queueIndex, VkQueue* pQueue) { (void)device; (void)queueFamilyIndex; (void)queueIndex; (void)pQueue; }
void vkCreateAndroidSurfaceKHR(VkInstance instance, const void* pCreateInfo, const void* pAllocator, VkSurfaceKHR* pSurface) { (void)instance; (void)pCreateInfo; (void)pAllocator; (void)pSurface; }
void vkDestroySurfaceKHR(VkInstance instance, VkSurfaceKHR surface, const void* pAllocator) { (void)instance; (void)surface; (void)pAllocator; }
void vkGetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, void* pSurfaceCapabilities) { (void)physicalDevice; (void)surface; (void)pSurfaceCapabilities; }
void vkGetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t* pSurfaceFormatCount, void* pSurfaceFormats) { (void)physicalDevice; (void)surface; (void)pSurfaceFormatCount; (void)pSurfaceFormats; }
void vkGetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t* pPresentModeCount, void* pPresentModes) { (void)physicalDevice; (void)surface; (void)pPresentModeCount; (void)pPresentModes; }
void vkCreatePipelineLayout(VkDevice device, const void* pCreateInfo, const void* pAllocator, VkPipelineLayout* pPipelineLayout) { (void)device; (void)pCreateInfo; (void)pAllocator; (void)pPipelineLayout; }
void vkDestroyPipelineLayout(VkDevice device, VkPipelineLayout pipelineLayout, const void* pAllocator) { (void)device; (void)pipelineLayout; (void)pAllocator; }
void vkCreateGraphicsPipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const void* pCreateInfos, const void* pAllocator, VkPipeline* pPipelines) { (void)device; (void)pipelineCache; (void)createInfoCount; (void)pCreateInfos; (void)pAllocator; (void)pPipelines; }
void vkDestroyPipeline(VkDevice device, VkPipeline pipeline, const void* pAllocator) { (void)device; (void)pipeline; (void)pAllocator; }
void vkUpdateDescriptorSets(VkDevice device, uint32_t descriptorWriteCount, const void* pDescriptorWrites, uint32_t descriptorCopyCount, const void* pDescriptorCopies) { (void)device; (void)descriptorWriteCount; (void)pDescriptorWrites; (void)descriptorCopyCount; (void)pDescriptorCopies; }
#endif

// Internal state
struct GraphicsState {
    bool initialized = false;
    uint32_t width = 0;
    uint32_t height = 0;
    ANativeWindow* window = nullptr;

    // Vulkan objects
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainImageViews;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkSemaphore imageAvailableSemaphore = VK_NULL_HANDLE;
    VkSemaphore renderFinishedSemaphore = VK_NULL_HANDLE;
    VkFence inFlightFence = VK_NULL_HANDLE;
    uint32_t currentFrame = 0;
    uint32_t imageIndex = 0;

    // Phase 6: Pipeline cache for faster pipeline creation
    VkPipelineCache pipelineCache = VK_NULL_HANDLE;

    // Phase 6: Descriptor pool and sets for efficient resource binding
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptorSets;

    // Phase 6: Frame pacing
    std::chrono::steady_clock::time_point lastFrameTime;
    uint32_t targetFPS = 60;  // 60 FPS default, can be 30 for power saving
    double frameTimeMs = 16.67;  // 1000/60
    bool enableFramePacing = true;

    // Phase 6: Performance monitoring
    struct PerfStats {
        uint64_t frameCount = 0;
        double currentFPS = 0.0;
        double avgFrameTimeMs = 0.0;
        double minFrameTimeMs = 9999.0;
        double maxFrameTimeMs = 0.0;
        std::chrono::steady_clock::time_point lastFPSUpdate;
        uint64_t framesSinceUpdate = 0;
    } perfStats;

    std::mutex stateMutex;
};

static GraphicsState g_state;

// Helper: Find memory type index
__attribute__((unused))
static uint32_t find_memory_type(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    (void)typeFilter; (void)properties;
    // In a real implementation, query VkPhysicalDeviceMemoryProperties
    // For now, return 0 as placeholder
    return 0;
}

// Helper: Create buffer
__attribute__((unused))
static VkResult create_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
                              VkMemoryPropertyFlags properties,
                              VkBuffer* buffer, VkDeviceMemory* bufferMemory) {
    (void)size; (void)usage; (void)properties; (void)buffer; (void)bufferMemory;
    // Placeholder - real implementation would create VkBuffer and allocate memory
    return VK_SUCCESS;
}

// Phase 6: Create pipeline cache
__attribute__((unused))
static VkResult create_pipeline_cache() {
    if (!g_state.initialized || g_state.device == VK_NULL_HANDLE) {
        return VK_SUCCESS;
    }

    VkPipelineCacheCreateInfo cacheInfo = {};
    cacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    cacheInfo.flags = 0;
    cacheInfo.initialDataSize = 0;
    cacheInfo.pInitialData = nullptr;

    VkResult result = vkCreatePipelineCache(g_state.device, &cacheInfo, nullptr, &g_state.pipelineCache);
    return result;
}

// Phase 6: Create descriptor pool for efficient resource binding
__attribute__((unused))
static VkResult create_descriptor_pool() {
    if (!g_state.initialized || g_state.device == VK_NULL_HANDLE) {
        return VK_SUCCESS;
    }

    // Pool sizes for common descriptor types
    VkDescriptorPoolSize poolSizes[2] = {};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = 16;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = 16;

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 32;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;

    VkResult result = vkCreateDescriptorPool(g_state.device, &poolInfo, nullptr, &g_state.descriptorPool);
    return result;
}

// Phase 6: Create descriptor set layout
__attribute__((unused))
static VkResult create_descriptor_set_layout() {
    if (!g_state.initialized || g_state.device == VK_NULL_HANDLE) {
        return VK_SUCCESS;
    }

    VkDescriptorSetLayoutBinding bindings[2] = {};
    // Binding 0: Uniform buffer (MVP matrices, etc.)
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    bindings[0].pImmutableSamplers = nullptr;

    // Binding 1: Combined image sampler (textures)
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[1].pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 2;
    layoutInfo.pBindings = bindings;

    VkResult result = vkCreateDescriptorSetLayout(g_state.device, &layoutInfo, nullptr, &g_state.descriptorSetLayout);
    return result;
}

// Phase 6: Allocate descriptor sets
__attribute__((unused))
static VkResult allocate_descriptor_sets(uint32_t count) {
    if (!g_state.initialized || g_state.device == VK_NULL_HANDLE || g_state.descriptorPool == VK_NULL_HANDLE || g_state.descriptorSetLayout == VK_NULL_HANDLE) {
        return VK_SUCCESS;
    }

    std::vector<VkDescriptorSetLayout> layouts(count, g_state.descriptorSetLayout);
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = g_state.descriptorPool;
    allocInfo.descriptorSetCount = count;
    allocInfo.pSetLayouts = layouts.data();

    g_state.descriptorSets.resize(count);
    VkResult result = vkAllocateDescriptorSets(g_state.device, &allocInfo, g_state.descriptorSets.data());
    return result;
}

// Phase 6: Update performance stats
static void update_perf_stats(double frameTimeMs) {
    auto& stats = g_state.perfStats;
    stats.frameCount++;
    stats.framesSinceUpdate++;

    if (frameTimeMs < stats.minFrameTimeMs) stats.minFrameTimeMs = frameTimeMs;
    if (frameTimeMs > stats.maxFrameTimeMs) stats.maxFrameTimeMs = frameTimeMs;

    // Update FPS every second
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration<double>(now - stats.lastFPSUpdate).count();
    if (elapsed >= 1.0) {
        stats.currentFPS = stats.framesSinceUpdate / elapsed;
        stats.lastFPSUpdate = now;
        stats.framesSinceUpdate = 0;
    }

    // Running average (exponential moving average)
    if (stats.avgFrameTimeMs == 0.0) {
        stats.avgFrameTimeMs = frameTimeMs;
    } else {
        stats.avgFrameTimeMs = stats.avgFrameTimeMs * 0.9 + frameTimeMs * 0.1;
    }
}

// Phase 6: Frame pacing - wait for target frame time
static void frame_pacing() {
    if (!g_state.enableFramePacing) return;

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration<double, std::milli>(now - g_state.lastFrameTime).count();

    if (elapsed < g_state.frameTimeMs) {
        // Busy wait for remaining time (could use sleep for longer waits)
        double remaining = g_state.frameTimeMs - elapsed;
        if (remaining > 2.0) {
            // Sleep for most of the remaining time
            std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(remaining - 1.0)));
        }
        // Spin for the last bit
        while (std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - g_state.lastFrameTime).count() < g_state.frameTimeMs) {
            // busy wait
        }
    }

    g_state.lastFrameTime = std::chrono::steady_clock::now();
}

// Initialize graphics subsystem
extern "C" bool graphics_init(int width, int height, HWND hwnd) {
    if (g_state.initialized) {
        return true; // Already initialized
    }

    g_state.width = static_cast<uint32_t>(width);
    g_state.height = static_cast<uint32_t>(height);

#if defined(__ANDROID__)
    // On Android, hwnd is actually an ANativeWindow* from Java/Kotlin
    g_state.window = reinterpret_cast<ANativeWindow*>(hwnd);
    if (!g_state.window) {
        return false;
    }
    ANativeWindow_acquire(g_state.window);
#else
    // Non-Android: hwnd is ignored in stub
    (void)hwnd;
#endif

#if defined(__ANDROID__)
    ROTTR_GFX_LOGE("Vulkan setup is not implemented: instance/device/swapchain/render pass are TODO; blank output is expected");
    ROTTR_GFX_LOGI("Native surface received: %dx%d window=%p", width, height, static_cast<void*>(g_state.window));
#endif
    // TODO: Full Vulkan initialization sequence:
    // 1. Create VkInstance with required extensions (VK_KHR_surface, VK_KHR_android_surface)
    // 2. Create VkSurfaceKHR from ANativeWindow
    // 3. Pick physical device with graphics queue
    // 4. Create logical device
    // 5. Create swapchain (with VK_PRESENT_MODE_MAILBOX_KHR for low latency)
    // 6. Create image views
    // 7. Create render pass
    // 8. Create framebuffers
    // 9. Create command pool and buffers
    // 10. Create synchronization objects (semaphores, fences)
    // 11. Create pipeline cache
    // 12. Create descriptor pool and layout
    // 13. Allocate descriptor sets

    // Phase 6: Initialize frame pacing
    g_state.frameTimeMs = 1000.0 / g_state.targetFPS;
    g_state.lastFrameTime = std::chrono::steady_clock::now();
    g_state.perfStats.lastFPSUpdate = std::chrono::steady_clock::now();

    g_state.initialized = true;
    return true;
}

// Shutdown graphics subsystem
extern "C" void graphics_shutdown(void) {
    if (!g_state.initialized) {
        return;
    }

    // Phase 6: Cleanup pipeline cache
    if (g_state.pipelineCache != VK_NULL_HANDLE) {
        vkDestroyPipelineCache(g_state.device, g_state.pipelineCache, nullptr);
        g_state.pipelineCache = VK_NULL_HANDLE;
    }

    // Phase 6: Cleanup descriptor pool
    if (g_state.descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(g_state.device, g_state.descriptorPool, nullptr);
        g_state.descriptorPool = VK_NULL_HANDLE;
    }

    if (g_state.descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(g_state.device, g_state.descriptorSetLayout, nullptr);
        g_state.descriptorSetLayout = VK_NULL_HANDLE;
    }

    // TODO: Cleanup Vulkan resources in reverse order
    // vkDestroyFence, vkDestroySemaphore, vkDestroyCommandPool
    // vkDestroyFramebuffer, vkDestroyRenderPass, vkDestroyImageView
    // vkDestroySwapchainKHR, vkDestroySurfaceKHR, vkDestroyDevice, vkDestroyInstance

    if (g_state.window) {
        ANativeWindow_release(g_state.window);
        g_state.window = nullptr;
    }

    std::memset(&g_state, 0, sizeof(g_state));
}

// Present the rendered frame to screen
extern "C" bool graphics_present(void) {
    if (!g_state.initialized) {
        return false;
    }

    // Phase 6: Frame pacing
    frame_pacing();

#if defined(__ANDROID__)
    static bool presentWarningLogged = false;
    if (!presentWarningLogged) {
        ROTTR_GFX_LOGE("graphics_present has no Vulkan acquire/submit/present implementation");
        presentWarningLogged = true;
    }
#endif
    // TODO: Full Vulkan present sequence:
    // 1. vkWaitForFences
    // 2. vkAcquireNextImageKHR
    // 3. Record command buffer (draw calls)
    // 4. vkQueueSubmit
    // 5. vkQueuePresentKHR

    return true;
}

// Phase 6: Render a video frame from YUV data
extern "C" bool graphics_render_frame(const VideoSample& sample) {
    if (!g_state.initialized) {
        return false;
    }

    auto frameStart = std::chrono::steady_clock::now();

    // TODO: Convert YUV to RGBA and render as textured quad
    // This would involve:
    // 1. Create/update a texture with the video data
    // 2. Bind pipeline for video rendering
    // 3. Bind descriptor sets
    // 4. Draw full-screen quad with texture

    (void)sample; // Suppress unused warning

    // Phase 6: Update performance stats
    auto frameEnd = std::chrono::steady_clock::now();
    double frameTimeMs = std::chrono::duration<double, std::milli>(frameEnd - frameStart).count();
    update_perf_stats(frameTimeMs);

    return true;
}

// Get the current back buffer texture handle
extern "C" void* graphics_get_back_buffer(void) {
    if (!g_state.initialized || g_state.swapchainImages.empty()) {
        return nullptr;
    }
    // Return the current swapchain image
    return g_state.swapchainImages[g_state.imageIndex];
}

// Get graphics adapter info
extern "C" void graphics_get_adapter_info(int& adapter_id, char* name, int max_name_len) {
    adapter_id = 0;
    if (name && max_name_len > 0) {
        const char* adapter_name = "Vulkan Graphics Adapter";
        std::strncpy(name, adapter_name, max_name_len - 1);
        name[max_name_len - 1] = '\0';
    }
}

// Phase 6: Performance monitoring API
extern "C" void graphics_get_perf_stats(double* out_fps, double* out_avg_frame_ms, double* out_min_frame_ms, double* out_max_frame_ms) {
    if (!g_state.initialized) {
        if (out_fps) *out_fps = 0.0;
        if (out_avg_frame_ms) *out_avg_frame_ms = 0.0;
        if (out_min_frame_ms) *out_min_frame_ms = 0.0;
        if (out_max_frame_ms) *out_max_frame_ms = 0.0;
        return;
    }

    std::lock_guard<std::mutex> lock(g_state.stateMutex);
    auto& stats = g_state.perfStats;

    if (out_fps) *out_fps = stats.currentFPS;
    if (out_avg_frame_ms) *out_avg_frame_ms = stats.avgFrameTimeMs;
    if (out_min_frame_ms) *out_min_frame_ms = stats.minFrameTimeMs;
    if (out_max_frame_ms) *out_max_frame_ms = stats.maxFrameTimeMs;
}

// Phase 6: Set target FPS for frame pacing (30 or 60)
extern "C" void graphics_set_target_fps(uint32_t fps) {
    if (!g_state.initialized) return;

    std::lock_guard<std::mutex> lock(g_state.stateMutex);
    if (fps == 30 || fps == 60) {
        g_state.targetFPS = fps;
        g_state.frameTimeMs = 1000.0 / fps;
    }
}

// Phase 6: Enable/disable frame pacing
extern "C" void graphics_set_frame_pacing(bool enable) {
    if (!g_state.initialized) return;
    std::lock_guard<std::mutex> lock(g_state.stateMutex);
    g_state.enableFramePacing = enable;
}

// --- D3D11 Translation Stubs ---
// These functions would be called by lifted game code
// They translate D3D11 API calls to Vulkan equivalents

extern "C" void* d3d11_create_device(void* adapter, int driver_type, void* software,
                                      uint32_t flags, void* feature_levels, uint32_t feature_levels_count,
                                      uint32_t sdk_version, void** device, void** feature_level,
                                      void** immediate_context) {
    (void)adapter; (void)driver_type; (void)software; (void)flags;
    (void)feature_levels; (void)feature_levels_count; (void)sdk_version;
    (void)device; (void)feature_level; (void)immediate_context;
    // TODO: Create D3D11 device -> maps to Vulkan device
    return nullptr;
}

extern "C" void* d3d11_create_swap_chain(void* factory, void* device, void* desc, void** swap_chain) {
    (void)factory; (void)device; (void)desc; (void)swap_chain;
    // TODO: Create swap chain with MAILBOX present mode for low latency
    return nullptr;
}

extern "C" void* d3d11_create_buffer(void* device, const D3D11_BUFFER_DESC* desc, void* initial_data, void** buffer) {
    (void)device; (void)desc; (void)initial_data; (void)buffer;
    // TODO: Create buffer (vertex, index, constant)
    return nullptr;
}

extern "C" void* d3d11_create_texture2d(void* device, void* desc, void* initial_data, void** texture) {
    (void)device; (void)desc; (void)initial_data; (void)texture;
    // TODO: Create 2D texture
    return nullptr;
}

extern "C" void* d3d11_create_shader_resource_view(void* device, void* resource, void* desc, void** view) {
    (void)device; (void)resource; (void)desc; (void)view;
    // TODO: Create shader resource view (texture binding)
    return nullptr;
}

extern "C" void* d3d11_create_render_target_view(void* device, void* resource, void* desc, void** view) {
    (void)device; (void)resource; (void)desc; (void)view;
    // TODO: Create render target view
    return nullptr;
}

extern "C" void* d3d11_create_depth_stencil_view(void* device, void* resource, void* desc, void** view) {
    (void)device; (void)resource; (void)desc; (void)view;
    // TODO: Create depth stencil view
    return nullptr;
}

extern "C" void* d3d11_create_vertex_shader(void* device, const void* bytecode, size_t bytecode_length, void* class_linkage, void** shader) {
    (void)device; (void)bytecode; (void)bytecode_length; (void)class_linkage; (void)shader;
    // TODO: Create vertex shader -> SPIR-V
    return nullptr;
}

extern "C" void* d3d11_create_pixel_shader(void* device, const void* bytecode, size_t bytecode_length, void* class_linkage, void** shader) {
    (void)device; (void)bytecode; (void)bytecode_length; (void)class_linkage; (void)shader;
    // TODO: Create pixel shader -> SPIR-V
    return nullptr;
}

extern "C" void* d3d11_create_input_layout(void* device, void* input_element_descs, uint32_t num_elements,
                                            const void* shader_bytecode, size_t bytecode_length, void** layout) {
    (void)device; (void)input_element_descs; (void)num_elements;
    (void)shader_bytecode; (void)bytecode_length; (void)layout;
    // TODO: Create input layout -> Vulkan pipeline vertex input
    return nullptr;
}

extern "C" void* d3d11_create_sampler_state(void* device, void* desc, void** sampler) {
    (void)device; (void)desc; (void)sampler;
    // TODO: Create sampler state
    return nullptr;
}

extern "C" void* d3d11_create_rasterizer_state(void* device, void* desc, void** state) {
    (void)device; (void)desc; (void)state;
    // TODO: Create rasterizer state
    return nullptr;
}

extern "C" void* d3d11_create_blend_state(void* device, void* desc, void** state) {
    (void)device; (void)desc; (void)state;
    // TODO: Create blend state
    return nullptr;
}

extern "C" void* d3d11_create_depth_stencil_state(void* device, const D3D11_DEPTH_STENCIL_DESC* desc, void** state) {
    (void)device; (void)desc; (void)state;
    // TODO: Create depth stencil state
    return nullptr;
}

extern "C" void d3d11_immediate_context_draw(void* context, uint32_t vertex_count, uint32_t start_vertex) {
    (void)context; (void)vertex_count; (void)start_vertex;
    // TODO: Draw call
}

extern "C" void d3d11_immediate_context_draw_indexed(void* context, uint32_t index_count, uint32_t start_index, int base_vertex) {
    (void)context; (void)index_count; (void)start_index; (void)base_vertex;
    // TODO: Draw indexed call
}

extern "C" void d3d11_immediate_context_clear_render_target_view(void* context, void* view, const float color[4]) {
    (void)context; (void)view; (void)color;
    // TODO: Clear render target
}

extern "C" void d3d11_immediate_context_clear_depth_stencil_view(void* context, void* view, uint32_t flags, float depth, uint8_t stencil) {
    (void)context; (void)view; (void)flags; (void)depth; (void)stencil;
    // TODO: Clear depth stencil
}

extern "C" void d3d11_immediate_context_ia_set_vertex_buffers(void* context, uint32_t start_slot, uint32_t num_buffers,
                                                               void** buffers, const uint32_t* strides, const uint32_t* offsets) {
    (void)context; (void)start_slot; (void)num_buffers;
    (void)buffers; (void)strides; (void)offsets;
    // TODO: Set vertex buffers
}

extern "C" void d3d11_immediate_context_ia_set_index_buffer(void* context, void* buffer, int format, uint32_t offset) {
    (void)context; (void)buffer; (void)format; (void)offset;
    // TODO: Set index buffer
}

extern "C" void d3d11_immediate_context_ia_set_primitive_topology(void* context, int topology) {
    (void)context; (void)topology;
    // TODO: Set primitive topology
}

extern "C" void d3d11_immediate_context_vs_set_shader(void* context, void* shader, void** class_instances, uint32_t num_class_instances) {
    (void)context; (void)shader; (void)class_instances; (void)num_class_instances;
    // TODO: Set vertex shader
}

extern "C" void d3d11_immediate_context_ps_set_shader(void* context, void* shader, void** class_instances, uint32_t num_class_instances) {
    (void)context; (void)shader; (void)class_instances; (void)num_class_instances;
    // TODO: Set pixel shader
}

extern "C" void d3d11_immediate_context_ps_set_shader_resources(void* context, uint32_t start_slot, uint32_t num_views, void** views) {
    (void)context; (void)start_slot; (void)num_views; (void)views;
    // TODO: Set shader resources (textures) - bind descriptor sets
}

extern "C" void d3d11_immediate_context_ps_set_samplers(void* context, uint32_t start_slot, uint32_t num_samplers, void** samplers) {
    (void)context; (void)start_slot; (void)num_samplers; (void)samplers;
    // TODO: Set samplers - bind descriptor sets
}

extern "C" void d3d11_immediate_context_om_set_render_targets(void* context, uint32_t num_views, void** views, void* depth_stencil) {
    (void)context; (void)num_views; (void)views; (void)depth_stencil;
    // TODO: Set render targets
}

extern "C" void d3d11_immediate_context_rsset_state(void* context, void* state) {
    (void)context; (void)state;
    // TODO: Set rasterizer state
}

extern "C" void d3d11_immediate_context_om_set_blend_state(void* context, void* state, const float blend_factor[4], uint32_t sample_mask) {
    (void)context; (void)state; (void)blend_factor; (void)sample_mask;
    // TODO: Set blend state
}

extern "C" void d3d11_immediate_context_om_set_depth_stencil_state(void* context, void* state, uint32_t stencil_ref) {
    (void)context; (void)state; (void)stencil_ref;
    // TODO: Set depth stencil state
}

extern "C" void d3d11_immediate_context_update_subresource(void* context, void* dst_resource, uint32_t dst_subresource,
                                                            void* dst_box, const void* src_data, uint32_t src_row_pitch, uint32_t src_depth_pitch) {
    (void)context; (void)dst_resource; (void)dst_subresource;
    (void)dst_box; (void)src_data; (void)src_row_pitch; (void)src_depth_pitch;
    // TODO: Update subresource (constant buffers, textures)
}