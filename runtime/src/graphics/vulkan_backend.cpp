#include "graphics/vulkan_backend.h"
#include "graphics/dxgi_vk_format.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <vector>

namespace rottr::gfx::d3d11 {
namespace {
VkAccessFlags accessForLayout(VkImageLayout layout) {
    switch (layout) {
    case VK_IMAGE_LAYOUT_UNDEFINED: return 0;
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL: return VK_ACCESS_TRANSFER_WRITE_BIT;
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL: return VK_ACCESS_TRANSFER_READ_BIT;
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL: return VK_ACCESS_SHADER_READ_BIT;
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
        return VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
        return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    default: return VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    }
}
VkImageLayout readableLayout(uint32_t bindFlags) {
    return (bindFlags & BindShaderResource) ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL;
}
}

VulkanBackend::VulkanBackend(VkPhysicalDevice physical, VkDevice device, VkQueue queue, uint32_t family)
    : physical_(physical), device_(device), queue_(queue) {
    if (device_ == VK_NULL_HANDLE || queue_ == VK_NULL_HANDLE) return;
    VkCommandPoolCreateInfo info{}; info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
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
    VkCommandBufferAllocateInfo alloc{}; alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc.commandPool = pool_; alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; alloc.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(device_, &alloc, &command) != VK_SUCCESS) return false;
    VkCommandBufferBeginInfo begin{}; begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(command, &begin) != VK_SUCCESS) {
        vkFreeCommandBuffers(device_, pool_, 1, &command); command = VK_NULL_HANDLE; return false;
    }
    return true;
}
bool VulkanBackend::endCommands(VkCommandBuffer command) {
    if (vkEndCommandBuffer(command) != VK_SUCCESS) { vkFreeCommandBuffers(device_, pool_, 1, &command); return false; }
    VkSubmitInfo submit{}; submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
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
    VkBufferCreateInfo info{}; info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = desc.byteWidth; info.usage = usage; info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device_, &info, nullptr, &a.buffer) != VK_SUCCESS) return false;
    VkMemoryRequirements req{}; vkGetBufferMemoryRequirements(device_, a.buffer, &req);
    const uint32_t type = memoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (type == UINT32_MAX) { destroyAllocation(a); return false; }
    VkMemoryAllocateInfo mem{}; mem.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; mem.allocationSize = req.size; mem.memoryTypeIndex = type;
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
    handle = 0; if (!valid() || desc.width == 0 || desc.height == 0 ||
        desc.mipLevels == 0 || desc.arraySize == 0 || desc.sampleCount == 0) return false;
    const FormatInfo* format = findFormat(desc.format); if (!format) return false;
    uint32_t maxDimension = std::max(desc.width, desc.height);
    uint32_t maxMipLevels = 1;
    while (maxDimension > 1) { maxDimension >>= 1U; ++maxMipLevels; }
    if (desc.mipLevels > maxMipLevels || (initial && (desc.sampleCount != 1 || format->depthStencil))) return false;
    Allocation a; a.kind = ResourceKind::Texture2D; a.width = desc.width; a.height = desc.height;
    a.mipLevels = desc.mipLevels; a.arraySize = desc.arraySize; a.format = static_cast<VkFormat>(format->vkFormat);
    a.bindFlags = desc.bindFlags; a.samples = static_cast<VkSampleCountFlagBits>(desc.sampleCount);
    a.subresourceLayouts.assign(static_cast<size_t>(desc.mipLevels) * desc.arraySize, VK_IMAGE_LAYOUT_UNDEFINED);
    a.aspect = format->depthStencil ? (desc.format == DxgiFormat::D24UnormS8Uint ? (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT) : VK_IMAGE_ASPECT_DEPTH_BIT) : VK_IMAGE_ASPECT_COLOR_BIT;
    VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (desc.bindFlags & BindShaderResource) usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (desc.bindFlags & BindRenderTarget) usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (desc.bindFlags & BindDepthStencil) usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if (desc.bindFlags & BindUnorderedAccess) usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    VkImageCreateInfo image{}; image.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image.imageType = VK_IMAGE_TYPE_2D; image.format = a.format;
    image.extent = {desc.width, desc.height, 1}; image.mipLevels = desc.mipLevels; image.arrayLayers = desc.arraySize;
    image.samples = static_cast<VkSampleCountFlagBits>(desc.sampleCount);
    image.tiling = VK_IMAGE_TILING_OPTIMAL; image.usage = usage; image.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(device_, &image, nullptr, &a.image) != VK_SUCCESS) return false;
    VkMemoryRequirements req{}; vkGetImageMemoryRequirements(device_, a.image, &req);
    const uint32_t type = memoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (type == UINT32_MAX) { destroyAllocation(a); return false; }
    VkMemoryAllocateInfo mem{}; mem.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; mem.allocationSize = req.size; mem.memoryTypeIndex = type;
    if (vkAllocateMemory(device_, &mem, nullptr, &a.memory) != VK_SUCCESS ||
        vkBindImageMemory(device_, a.image, a.memory, 0) != VK_SUCCESS) { destroyAllocation(a); return false; }
    VkImageViewCreateInfo view{}; view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view.image = a.image; view.viewType = desc.arraySize > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
    view.format = a.format; view.subresourceRange = {a.aspect, 0, desc.mipLevels, 0, desc.arraySize};
    if (vkCreateImageView(device_, &view, nullptr, &a.view) != VK_SUCCESS) { destroyAllocation(a); return false; }
    if (initial) {
        if (desc.sampleCount != 1) { destroyAllocation(a); return false; }
        const uint64_t subresourceCount64 = static_cast<uint64_t>(desc.mipLevels) * desc.arraySize;
        if (subresourceCount64 > std::numeric_limits<uint32_t>::max()) { destroyAllocation(a); return false; }
        const uint32_t subresourceCount = static_cast<uint32_t>(subresourceCount64);
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(physical_, &properties);
        VkDeviceSize alignment = std::max<VkDeviceSize>(4, properties.limits.optimalBufferCopyOffsetAlignment);
        alignment = std::max<VkDeviceSize>(alignment, format->bytesPerBlock);
        struct Upload { VkBufferImageCopy region; const uint8_t* data; uint32_t rowPitch; uint32_t rows; VkDeviceSize offset; };
        std::vector<Upload> uploads; uploads.reserve(subresourceCount);
        VkDeviceSize stagingSize = 0;
        for (uint32_t layer = 0; layer < desc.arraySize; ++layer) {
            for (uint32_t mip = 0; mip < desc.mipLevels; ++mip) {
                const uint32_t index = layer * desc.mipLevels + mip;
                const SubresourceData& sub = initial[index];
                const uint32_t width = std::max(1U, desc.width >> mip);
                const uint32_t height = std::max(1U, desc.height >> mip);
                const uint32_t blocksX = width / format->blockWidth + (width % format->blockWidth != 0U);
                const uint32_t rows = height / format->blockHeight + (height % format->blockHeight != 0U);
                const uint64_t tightRow64 = static_cast<uint64_t>(blocksX) * format->bytesPerBlock;
                if (!sub.data || tightRow64 == 0 || tightRow64 > UINT32_MAX) { destroyAllocation(a); return false; }
                const uint32_t tightRow = static_cast<uint32_t>(tightRow64);
                const uint32_t sourceRow = sub.rowPitch ? sub.rowPitch : tightRow;
                const uint64_t minSlice = static_cast<uint64_t>(sourceRow) * rows;
                const uint64_t sourceSlice = sub.slicePitch ? sub.slicePitch : minSlice;
                if (sourceRow < tightRow || minSlice > sourceSlice ||
                    static_cast<uint64_t>(sourceRow) * (rows - 1U) + tightRow > sourceSlice) { destroyAllocation(a); return false; }
                const VkDeviceSize mask = alignment - 1;
                if ((alignment & mask) != 0 || stagingSize > std::numeric_limits<VkDeviceSize>::max() - mask) { destroyAllocation(a); return false; }
                stagingSize = (stagingSize + mask) & ~mask;
                const VkDeviceSize bytes = static_cast<VkDeviceSize>(tightRow) * rows;
                if (bytes > std::numeric_limits<VkDeviceSize>::max() - stagingSize) { destroyAllocation(a); return false; }
                Upload upload{}; upload.data = static_cast<const uint8_t*>(sub.data); upload.rowPitch = sourceRow; upload.rows = rows; upload.offset = stagingSize;
                upload.region.bufferOffset = stagingSize;
                upload.region.bufferRowLength = 0; upload.region.bufferImageHeight = 0;
                upload.region.imageSubresource = {a.aspect, mip, layer, 1};
                upload.region.imageOffset = {0, 0, 0}; upload.region.imageExtent = {width, height, 1};
                uploads.push_back(upload); stagingSize += bytes;
            }
        }
        Allocation staging{};
        VkBufferCreateInfo stagingInfo{}; stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        stagingInfo.size = stagingSize; stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT; stagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(device_, &stagingInfo, nullptr, &staging.buffer) != VK_SUCCESS) { destroyAllocation(a); return false; }
        VkMemoryRequirements stagingReq{}; vkGetBufferMemoryRequirements(device_, staging.buffer, &stagingReq);
        uint32_t stagingType = memoryType(stagingReq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (stagingType == UINT32_MAX) { destroyAllocation(staging); destroyAllocation(a); return false; }
        VkMemoryAllocateInfo stagingMem{}; stagingMem.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        stagingMem.allocationSize = stagingReq.size; stagingMem.memoryTypeIndex = stagingType;
        if (vkAllocateMemory(device_, &stagingMem, nullptr, &staging.memory) != VK_SUCCESS ||
            vkBindBufferMemory(device_, staging.buffer, staging.memory, 0) != VK_SUCCESS) { destroyAllocation(staging); destroyAllocation(a); return false; }
        void* mapped = nullptr;
        if (vkMapMemory(device_, staging.memory, 0, stagingSize, 0, &mapped) != VK_SUCCESS) { destroyAllocation(staging); destroyAllocation(a); return false; }
        for (const Upload& upload : uploads) {
            uint8_t* dst = static_cast<uint8_t*>(mapped) + upload.offset;
            for (uint32_t row = 0; row < upload.rows; ++row)
                std::memcpy(dst + static_cast<size_t>(row) * (upload.region.imageExtent.width / format->blockWidth + (upload.region.imageExtent.width % format->blockWidth != 0U)) * format->bytesPerBlock,
                            upload.data + static_cast<size_t>(row) * upload.rowPitch,
                            static_cast<size_t>(upload.region.imageExtent.width / format->blockWidth + (upload.region.imageExtent.width % format->blockWidth != 0U)) * format->bytesPerBlock);
        }
        vkUnmapMemory(device_, staging.memory);
        VkCommandBuffer command = VK_NULL_HANDLE;
        if (!beginCommands(command)) { destroyAllocation(staging); destroyAllocation(a); return false; }
        VkImageMemoryBarrier toTransfer{}; toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toTransfer.srcAccessMask = 0; toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.image = a.image; toTransfer.subresourceRange = {a.aspect, 0, desc.mipLevels, 0, desc.arraySize};
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toTransfer);
        std::vector<VkBufferImageCopy> regions; regions.reserve(uploads.size());
        for (const Upload& upload : uploads) regions.push_back(upload.region);
        vkCmdCopyBufferToImage(command, staging.buffer, a.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<uint32_t>(regions.size()), regions.data());
        VkImageMemoryBarrier toRead{}; toRead.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toRead.dstAccessMask = (desc.bindFlags & BindShaderResource) ? VK_ACCESS_SHADER_READ_BIT : VK_ACCESS_MEMORY_READ_BIT;
        toRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toRead.newLayout = (desc.bindFlags & BindShaderResource) ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL;
        toRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; toRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toRead.image = a.image; toRead.subresourceRange = {a.aspect, 0, desc.mipLevels, 0, desc.arraySize};
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &toRead);
        if (!endCommands(command)) { destroyAllocation(staging); destroyAllocation(a); return false; }
        destroyAllocation(staging);
        std::fill(a.subresourceLayouts.begin(), a.subresourceLayouts.end(), toRead.newLayout);
    }
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
    if (vkMapMemory(device_, it->second.memory, 0, it->second.size, 0, &mapped) != VK_SUCCESS) return false;
    std::memcpy(static_cast<uint8_t*>(mapped) + offset, data, size); vkUnmapMemory(device_, it->second.memory); return true;
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
    Allocation& dstImage = d->second;
    Allocation& srcImage = s->second;
    if (dstImage.width != srcImage.width || dstImage.height != srcImage.height ||
        dstImage.mipLevels != srcImage.mipLevels || dstImage.arraySize != srcImage.arraySize ||
        dstImage.format != srcImage.format || dstImage.samples != srcImage.samples ||
        dstImage.subresourceLayouts.size() != srcImage.subresourceLayouts.size() ||
        dstImage.samples != VK_SAMPLE_COUNT_1_BIT) return false;

    const uint32_t count = dstImage.mipLevels * dstImage.arraySize;
    std::vector<VkImageMemoryBarrier> toTransfer;
    std::vector<VkImageMemoryBarrier> restore;
    std::vector<VkImageCopy> regions;
    toTransfer.reserve(static_cast<size_t>(count) * 2U);
    restore.reserve(static_cast<size_t>(count) * 2U);
    regions.reserve(count);
    std::vector<VkImageLayout> dstFinal(dstImage.subresourceLayouts.size());
    for (uint32_t layer = 0; layer < dstImage.arraySize; ++layer) {
        for (uint32_t mip = 0; mip < dstImage.mipLevels; ++mip) {
            const size_t index = static_cast<size_t>(layer) * dstImage.mipLevels + mip;
            const VkImageLayout srcOld = srcImage.subresourceLayouts[index];
            const VkImageLayout dstOld = dstImage.subresourceLayouts[index];
            if (srcOld == VK_IMAGE_LAYOUT_UNDEFINED) return false;
            const VkImageLayout finalDst = dstOld == VK_IMAGE_LAYOUT_UNDEFINED
                ? readableLayout(dstImage.bindFlags) : dstOld;
            dstFinal[index] = finalDst;

            VkImageMemoryBarrier srcBarrier{}; srcBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            srcBarrier.srcAccessMask = accessForLayout(srcOld); srcBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            srcBarrier.oldLayout = srcOld; srcBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            srcBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; srcBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            srcBarrier.image = srcImage.image; srcBarrier.subresourceRange = {srcImage.aspect, mip, 1, layer, 1};
            if (srcOld != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) toTransfer.push_back(srcBarrier);

            VkImageMemoryBarrier dstBarrier{}; dstBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            dstBarrier.srcAccessMask = accessForLayout(dstOld); dstBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            dstBarrier.oldLayout = dstOld; dstBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            dstBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; dstBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            dstBarrier.image = dstImage.image; dstBarrier.subresourceRange = {dstImage.aspect, mip, 1, layer, 1};
            if (dstOld != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) toTransfer.push_back(dstBarrier);

            VkImageCopy region{};
            region.srcSubresource = {srcImage.aspect, mip, layer, 1};
            region.dstSubresource = {dstImage.aspect, mip, layer, 1};
            region.extent = {std::max(1U, srcImage.width >> mip), std::max(1U, srcImage.height >> mip), 1};
            regions.push_back(region);

            if (srcOld != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
                srcBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                srcBarrier.dstAccessMask = accessForLayout(srcOld);
                srcBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                srcBarrier.newLayout = srcOld;
                restore.push_back(srcBarrier);
            }
            if (finalDst != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
                dstBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                dstBarrier.dstAccessMask = accessForLayout(finalDst);
                dstBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                dstBarrier.newLayout = finalDst;
                restore.push_back(dstBarrier);
            }
        }
    }

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (!beginCommands(cmd)) return false;
    if (!toTransfer.empty())
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, static_cast<uint32_t>(toTransfer.size()), toTransfer.data());
    vkCmdCopyImage(cmd, srcImage.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   dstImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   static_cast<uint32_t>(regions.size()), regions.data());
    if (!restore.empty())
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             0, 0, nullptr, 0, nullptr, static_cast<uint32_t>(restore.size()), restore.data());
    if (!endCommands(cmd)) return false;
    dstImage.subresourceLayouts = std::move(dstFinal);
    return true;
}

bool VulkanBackend::transitionTexture(uint64_t handle, VkImageLayout target) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = allocations_.find(handle);
    if (it == allocations_.end() || it->second.kind != ResourceKind::Texture2D ||
        target == VK_IMAGE_LAYOUT_UNDEFINED || target == VK_IMAGE_LAYOUT_PREINITIALIZED) return false;
    Allocation& image = it->second;
    std::vector<VkImageMemoryBarrier> barriers;
    barriers.reserve(image.subresourceLayouts.size());
    for (uint32_t layer = 0; layer < image.arraySize; ++layer) {
        for (uint32_t mip = 0; mip < image.mipLevels; ++mip) {
            const size_t index = static_cast<size_t>(layer) * image.mipLevels + mip;
            const VkImageLayout old = image.subresourceLayouts[index];
            if (old == target) continue;
            VkImageMemoryBarrier barrier{}; barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.srcAccessMask = accessForLayout(old); barrier.dstAccessMask = accessForLayout(target);
            barrier.oldLayout = old; barrier.newLayout = target;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = image.image; barrier.subresourceRange = {image.aspect, mip, 1, layer, 1};
            barriers.push_back(barrier);
        }
    }
    if (barriers.empty()) return true;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (!beginCommands(cmd)) return false;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         0, 0, nullptr, 0, nullptr, static_cast<uint32_t>(barriers.size()), barriers.data());
    if (!endCommands(cmd)) return false;
    std::fill(image.subresourceLayouts.begin(), image.subresourceLayouts.end(), target);
    return true;
}
// Draw needs a translated shader pair, input layout, render pass and pipeline state.
// Fail closed until that pipeline state is supplied; never report a fake successful draw.
bool VulkanBackend::draw(uint32_t, uint32_t, uint32_t, uint32_t) { return false; }
bool VulkanBackend::drawIndexed(uint32_t, uint32_t, int32_t, uint32_t) { return false; }

} // namespace rottr::gfx::d3d11
