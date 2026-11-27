#include "graphics/vulkan_backend.h"
#include "graphics/dxgi_vk_format.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <vector>

namespace rottr::gfx::d3d11 {

VulkanBackend::VulkanBackend(VkPhysicalDevice physical, VkDevice device, VkQueue queue, uint32_t family)
    : physical_(physical), device_(device), queue_(queue) {
    if (device_ == VK_NULL_HANDLE || queue_ == VK_NULL_HANDLE) return;
    VkCommandPoolCreateInfo info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    info.queueFamilyIndex = family;
    vkCreateCommandPool(device_, &info, nullptr, &pool_);
}
VulkanBackend::~VulkanBackend() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (device_ == VK_NULL_HANDLE) return;
    vkDeviceWaitIdle(device_);
    for (auto& entry : allocations_) destroyAllocation(entry.second);
    allocations_.clear();
    if (pool_ != VK_NULL_HANDLE) vkDestroyCommandPool(device_, pool_, nullptr);
}
bool VulkanBackend::valid() const {
    return physical_ != VK_NULL_HANDLE && device_ != VK_NULL_HANDLE &&
           queue_ != VK_NULL_HANDLE && pool_ != VK_NULL_HANDLE;
}
uint32_t VulkanBackend::memoryType(uint32_t bits, VkMemoryPropertyFlags required) const {
    VkPhysicalDeviceMemoryProperties props{};
    vkGetPhysicalDeviceMemoryProperties(physical_, &props);
    for (uint32_t i = 0; i < props.memoryTypeCount; ++i)
        if ((bits & (1U << i)) && (props.memoryTypes[i].propertyFlags & required) == required) return i;
    return UINT32_MAX;
}
bool VulkanBackend::beginCommands(VkCommandBuffer& command) {
    VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc.commandPool = pool_; alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; alloc.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(device_, &alloc, &command) != VK_SUCCESS) return false;
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(command, &begin) != VK_SUCCESS) {
        vkFreeCommandBuffers(device_, pool_, 1, &command); command = VK_NULL_HANDLE; return false;
    }
    return true;
}
bool VulkanBackend::endCommands(VkCommandBuffer command) {
    if (vkEndCommandBuffer(command) != VK_SUCCESS) { vkFreeCommandBuffers(device_, pool_, 1, &command); return false; }
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1; submit.pCommandBuffers = &command;
    const VkResult submitted = vkQueueSubmit(queue_, 1, &submit, VK_NULL_HANDLE);
    const VkResult waited = submitted == VK_SUCCESS ? vkQueueWaitIdle(queue_) : submitted;
    vkFreeCommandBuffers(device_, pool_, 1, &command);
    return waited == VK_SUCCESS;
}
void VulkanBackend::destroyAllocation(Allocation& a) {
    if (a.view) vkDestroyImageView(device_, a.view, nullptr);
    if (a.buffer) vkDestroyBuffer(device_, a.buffer, nullptr);
    if (a.image) vkDestroyImage(device_, a.image, nullptr);
    if (a.memory) vkFreeMemory(device_, a.memory, nullptr);
    a = {};
}
bool VulkanBackend::createBuffer(const BufferDesc& desc, const void* initial, uint64_t& handle) {
    std::lock_guard<std::mutex> lock(mutex_);
    handle = 0; if (!valid() || desc.byteWidth == 0) return false;
    Allocation a; a.kind = ResourceKind::Buffer; a.size = desc.byteWidth;
    VkBufferUsageFlags usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (desc.bindFlags & BindVertexBuffer) usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (desc.bindFlags & BindIndexBuffer) usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (desc.bindFlags & BindConstantBuffer) usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (desc.bindFlags & BindShaderResource) usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = desc.byteWidth; info.usage = usage; info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device_, &info, nullptr, &a.buffer) != VK_SUCCESS) return false;
    VkMemoryRequirements req{}; vkGetBufferMemoryRequirements(device_, a.buffer, &req);
    const uint32_t type = memoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (type == UINT32_MAX) { destroyAllocation(a); return false; }
    VkMemoryAllocateInfo mem{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; mem.allocationSize = req.size; mem.memoryTypeIndex = type;
    if (vkAllocateMemory(device_, &mem, nullptr, &a.memory) != VK_SUCCESS ||
        vkBindBufferMemory(device_, a.buffer, a.memory, 0) != VK_SUCCESS) { destroyAllocation(a); return false; }
    if (initial) {
        void* mapped = nullptr;
        if (vkMapMemory(device_, a.memory, 0, desc.byteWidth, 0, &mapped) != VK_SUCCESS) { destroyAllocation(a); return false; }
        std::memcpy(mapped, initial, desc.byteWidth); vkUnmapMemory(device_, a.memory);
    }
    handle = nextHandle_++; allocations_.emplace(handle, a); return true;
}
bool VulkanBackend::createTexture2D(const Texture2DDesc& desc, const SubresourceData* initial, uint64_t& handle) {
    std::lock_guard<std::mutex> lock(mutex_);
    handle = 0; if (!valid() || desc.width == 0 || desc.height == 0) return false;
    const FormatInfo* format = findFormat(desc.format); if (!format) return false;
    Allocation a; a.kind = ResourceKind::Texture2D; a.width = desc.width; a.height = desc.height;
    a.mipLevels = desc.mipLevels; a.arraySize = desc.arraySize; a.format = static_cast<VkFormat>(format->vkFormat);
    a.aspect = format->depthStencil ? (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT) : VK_IMAGE_ASPECT_COLOR_BIT;
    VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (desc.bindFlags & BindShaderResource) usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (desc.bindFlags & BindRenderTarget) usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (desc.bindFlags & BindDepthStencil) usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if (desc.bindFlags & BindUnorderedAccess) usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image.imageType = VK_IMAGE_TYPE_2D; image.format = a.format;
    image.extent = {desc.width, desc.height, 1}; image.mipLevels = desc.mipLevels; image.arrayLayers = desc.arraySize;
    image.samples = static_cast<VkSampleCountFlagBits>(desc.sampleCount);
    image.tiling = VK_IMAGE_TILING_OPTIMAL; image.usage = usage; image.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(device_, &image, nullptr, &a.image) != VK_SUCCESS) return false;
    VkMemoryRequirements req{}; vkGetImageMemoryRequirements(device_, a.image, &req);
    const uint32_t type = memoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (type == UINT32_MAX) { destroyAllocation(a); return false; }
    VkMemoryAllocateInfo mem{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; mem.allocationSize = req.size; mem.memoryTypeIndex = type;
    if (vkAllocateMemory(device_, &mem, nullptr, &a.memory) != VK_SUCCESS ||
        vkBindImageMemory(device_, a.image, a.memory, 0) != VK_SUCCESS) { destroyAllocation(a); return false; }
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = a.image; view.viewType = desc.arraySize > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
    view.format = a.format; view.subresourceRange = {a.aspect, 0, desc.mipLevels, 0, desc.arraySize};
    if (vkCreateImageView(device_, &view, nullptr, &a.view) != VK_SUCCESS) { destroyAllocation(a); return false; }
    // Initial texture upload is deliberately rejected until a complete per-mip/per-layer
    // staging layout is supplied; silently ignoring initial data would corrupt game textures.
    if (initial) { destroyAllocation(a); return false; }
    handle = nextHandle_++; allocations_.emplace(handle, a); return true;
}
void VulkanBackend::destroyResource(uint64_t handle) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = allocations_.find(handle); if (it == allocations_.end()) return;
    vkDeviceWaitIdle(device_); destroyAllocation(it->second); allocations_.erase(it);
}
bool VulkanBackend::uploadBuffer(uint64_t handle, uint32_t offset, const void* data, uint32_t size) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = allocations_.find(handle);
    if (it == allocations_.end() || it->second.kind != ResourceKind::Buffer || !data ||
        offset > it->second.size || size > it->second.size - offset) return false;
    void* mapped = nullptr;
    if (vkMapMemory(device_, it->second.memory, offset, size, 0, &mapped) != VK_SUCCESS) return false;
    std::memcpy(mapped, data, size); vkUnmapMemory(device_, it->second.memory); return true;
}
bool VulkanBackend::copyResource(uint64_t dst, uint64_t src) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto d = allocations_.find(dst), s = allocations_.find(src);
    if (d == allocations_.end() || s == allocations_.end() || d->second.kind != s->second.kind) return false;
    if (d->second.kind == ResourceKind::Buffer) {
        if (d->second.size != s->second.size) return false;
        VkCommandBuffer cmd = VK_NULL_HANDLE; if (!beginCommands(cmd)) return false;
        VkBufferCopy region{0, 0, d->second.size};
        vkCmdCopyBuffer(cmd, s->second.buffer, d->second.buffer, 1, &region);
        return endCommands(cmd);
    }
    if (d->second.width != s->second.width || d->second.height != s->second.height ||
        d->second.format != s->second.format || d->second.mipLevels != s->second.mipLevels ||
        d->second.arraySize != s->second.arraySize) return false;
    VkCommandBuffer cmd = VK_NULL_HANDLE; if (!beginCommands(cmd)) return false;
    VkImageMemoryBarrier barriers[2]{};
    for (auto& b : barriers) { b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER; b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; b.subresourceRange = {d->second.aspect, 0, d->second.mipLevels, 0, d->second.arraySize}; }
    barriers[0].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; barriers[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barriers[0].image = d->second.image; barriers[0].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barriers[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; barriers[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barriers[1].image = s->second.image; barriers[1].srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 2, barriers);
    VkImageCopy region{}; region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, d->second.arraySize};
    region.dstSubresource = region.srcSubresource; region.extent = {d->second.width, d->second.height, 1};
    vkCmdCopyImage(cmd, s->second.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, d->second.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    return endCommands(cmd);
}
// Draw needs a translated shader pair, input layout, render pass and pipeline state.
// Fail closed until that pipeline state is supplied; never report a fake successful draw.
bool VulkanBackend::draw(uint32_t, uint32_t, uint32_t, uint32_t) { return false; }
bool VulkanBackend::drawIndexed(uint32_t, uint32_t, int32_t, uint32_t) { return false; }

} // namespace rottr::gfx::d3d11
