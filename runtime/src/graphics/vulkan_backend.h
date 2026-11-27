#pragma once

#include "graphics/d3d11_device.h"
#include <vulkan/vulkan.h>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace rottr::gfx::d3d11 {

// Owns D3D11 resource allocations on an already-created Vulkan device.
// The caller must keep VkDevice/VkPhysicalDevice alive until this object is destroyed.
class VulkanBackend final : public Backend {
public:
    VulkanBackend(VkPhysicalDevice physical, VkDevice device, VkQueue queue, uint32_t queueFamily);
    ~VulkanBackend() override;
    VulkanBackend(const VulkanBackend&) = delete;
    VulkanBackend& operator=(const VulkanBackend&) = delete;

    bool valid() const;
    bool createBuffer(const BufferDesc&, const void*, uint64_t& handle) override;
    bool createTexture2D(const Texture2DDesc&, const SubresourceData*, uint64_t& handle) override;
    void destroyResource(uint64_t handle) override;
    bool uploadBuffer(uint64_t handle, uint32_t offset, const void*, uint32_t size) override;
    bool copyResource(uint64_t dst, uint64_t src) override;
    // Transitions every mip/layer of a texture and updates its tracked Vulkan layout.
    bool transitionTexture(uint64_t handle, VkImageLayout target);
    VkShaderModule createShaderModule(const uint32_t* spirvWords, size_t wordCount);
    void destroyShaderModule(VkShaderModule module);
    // Creates a real Vulkan graphics pipeline using caller-owned shader modules,
    // pipeline layout, render pass, and fixed-function state. Pointers are consumed
    // synchronously; the returned pipeline is owned by this backend.
    VkPipeline createGraphicsPipeline(const VkGraphicsPipelineCreateInfo& createInfo);
    void destroyGraphicsPipeline(VkPipeline pipeline);
    bool draw(uint32_t, uint32_t, uint32_t, uint32_t) override;
    bool drawIndexed(uint32_t, uint32_t, int32_t, uint32_t) override;

private:
    struct Allocation {
        ResourceKind kind = ResourceKind::Buffer;
        VkBuffer buffer = VK_NULL_HANDLE;
        VkImage image = VK_NULL_HANDLE;
        std::vector<VkImageLayout> subresourceLayouts;
        VkImageView view = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkDeviceSize size = 0;
        VkFormat format = VK_FORMAT_UNDEFINED;
        VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
        uint32_t width = 0, height = 0, mipLevels = 1, arraySize = 1;
        uint32_t bindFlags = 0;
        VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    };
    uint32_t memoryType(uint32_t bits, VkMemoryPropertyFlags required) const;
    bool beginCommands(VkCommandBuffer& command);
    bool endCommands(VkCommandBuffer command);
    void destroyAllocation(Allocation& allocation);

    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    VkCommandPool pool_ = VK_NULL_HANDLE;
    VkPipelineCache pipelineCache_ = VK_NULL_HANDLE;
    std::vector<VkPipeline> pipelines_;
    mutable std::mutex mutex_;
    uint64_t nextHandle_ = 1;
    std::unordered_map<uint64_t, Allocation> allocations_;
};

} // namespace rottr::gfx::d3d11
