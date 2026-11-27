#include "rottr/vulkan_renderer.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace rottr {
namespace {

constexpr uint64_t kAcquireTimeoutNs = 100'000'000;   // lets the render thread stop promptly
constexpr uint64_t kFenceTimeoutNs = 2'000'000'000;

}  // namespace

void VulkanRenderer::logf(bool isError, const char* format, ...) const {
    char text[512];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    if (log_ != nullptr) log_(isError, text);
}

bool VulkanRenderer::check(VkResult result, const char* what) {
    if (result == VK_SUCCESS) return true;
    logf(true, "%s failed: VkResult %d", what, static_cast<int>(result));
    return false;
}

bool VulkanRenderer::init(const char* surfaceExtension, const SurfaceFactory& createSurface,
                          uint32_t width, uint32_t height) {
    destroy();
    requested_ = {width, height};
    presented_ = 0;

    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "ROTTR-Recomp";
    app.apiVersion = VK_API_VERSION_1_1;
    const char* extensions[] = {VK_KHR_SURFACE_EXTENSION_NAME, surfaceExtension};
    VkInstanceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    info.pApplicationInfo = &app;
    info.enabledExtensionCount = 2;
    info.ppEnabledExtensionNames = extensions;

    const bool ok = check(vkCreateInstance(&info, nullptr, &instance_), "vkCreateInstance") &&
                    check(createSurface(instance_, &surface_), "create surface") &&
                    pickDevice() && createDevice() && createSwapchain();
    if (!ok) destroy();
    return ok;
}

bool VulkanRenderer::pickDevice() {
    uint32_t count = 0;
    if (!check(vkEnumeratePhysicalDevices(instance_, &count, nullptr), "enumerate devices")) return false;
    std::vector<VkPhysicalDevice> devices(count);
    if (count == 0 || vkEnumeratePhysicalDevices(instance_, &count, devices.data()) < 0) {
        logf(true, "no Vulkan device found");
        return false;
    }
    for (VkPhysicalDevice candidate : devices) {
        uint32_t families = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &families, nullptr);
        std::vector<VkQueueFamilyProperties> properties(families);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &families, properties.data());
        for (uint32_t i = 0; i < families; ++i) {
            VkBool32 present = VK_FALSE;
            if (vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, surface_, &present) != VK_SUCCESS)
                continue;
            if (present && (properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                physical_ = candidate;
                queueFamily_ = i;
                VkPhysicalDeviceProperties device{};
                vkGetPhysicalDeviceProperties(candidate, &device);
                logf(false, "GPU: %s, Vulkan %u.%u.%u, driver 0x%x", device.deviceName,
                     VK_API_VERSION_MAJOR(device.apiVersion), VK_API_VERSION_MINOR(device.apiVersion),
                     VK_API_VERSION_PATCH(device.apiVersion), device.driverVersion);
                return true;
            }
        }
    }
    logf(true, "no queue can both render and present to this surface");
    return false;
}

bool VulkanRenderer::createDevice() {
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queue{};
    queue.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue.queueFamilyIndex = queueFamily_;
    queue.queueCount = 1;
    queue.pQueuePriorities = &priority;
    const char* swapchainExtension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    VkDeviceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    info.queueCreateInfoCount = 1;
    info.pQueueCreateInfos = &queue;
    info.enabledExtensionCount = 1;
    info.ppEnabledExtensionNames = &swapchainExtension;
    if (!check(vkCreateDevice(physical_, &info, nullptr, &device_), "vkCreateDevice")) return false;
    vkGetDeviceQueue(device_, queueFamily_, 0, &queue_);

    VkCommandPoolCreateInfo pool{};
    pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool.queueFamilyIndex = queueFamily_;
    if (!check(vkCreateCommandPool(device_, &pool, nullptr, &pool_), "vkCreateCommandPool")) return false;
    VkCommandBufferAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocate.commandPool = pool_;
    allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate.commandBufferCount = 1;
    if (!check(vkAllocateCommandBuffers(device_, &allocate, &commands_), "allocate command buffer"))
        return false;

    VkSemaphoreCreateInfo semaphore{};
    semaphore.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fence{};
    fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    return check(vkCreateSemaphore(device_, &semaphore, nullptr, &imageReady_), "vkCreateSemaphore") &&
           check(vkCreateFence(device_, &fence, nullptr, &frameDone_), "vkCreateFence");
}

bool VulkanRenderer::createSwapchain() {
    VkSurfaceCapabilitiesKHR caps{};
    if (!check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_, surface_, &caps), "surface capabilities"))
        return false;

    uint32_t count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &count, formats.data());
    if (count == 0) {
        logf(true, "surface reports no formats");
        return false;
    }
    VkSurfaceFormatKHR chosen = formats[0];
    for (const VkSurfaceFormatKHR& f : formats) {
        if (f.format == VK_FORMAT_R8G8B8A8_UNORM || f.format == VK_FORMAT_B8G8R8A8_UNORM) {
            chosen = f;
            break;
        }
    }
    format_ = chosen.format;

    extent_ = caps.currentExtent;
    if (extent_.width == UINT32_MAX) {
        extent_.width = std::clamp(requested_.width, caps.minImageExtent.width, caps.maxImageExtent.width);
        extent_.height = std::clamp(requested_.height, caps.minImageExtent.height, caps.maxImageExtent.height);
    }
    if (extent_.width == 0 || extent_.height == 0) {
        logf(true, "surface has zero size");
        return false;
    }

    uint32_t images = std::max(caps.minImageCount + 1, 2u);
    if (caps.maxImageCount != 0) images = std::min(images, caps.maxImageCount);

    // Android commonly offers only INHERIT, so take whichever mode is supported.
    VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    for (VkCompositeAlphaFlagBitsKHR mode : {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
             VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
             VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR}) {
        if (caps.supportedCompositeAlpha & mode) {
            alpha = mode;
            break;
        }
    }

    VkSwapchainCreateInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    info.surface = surface_;
    info.minImageCount = images;
    info.imageFormat = chosen.format;
    info.imageColorSpace = chosen.colorSpace;
    info.imageExtent = extent_;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = (caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
                            ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR : caps.currentTransform;
    info.compositeAlpha = alpha;
    info.presentMode = VK_PRESENT_MODE_FIFO_KHR;  // always supported; paces to the display
    info.clipped = VK_TRUE;
    if (!check(vkCreateSwapchainKHR(device_, &info, nullptr, &swapchain_), "vkCreateSwapchainKHR"))
        return false;

    VkAttachmentDescription color{};
    color.format = format_;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentReference reference{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &reference;
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo pass{};
    pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    pass.attachmentCount = 1;
    pass.pAttachments = &color;
    pass.subpassCount = 1;
    pass.pSubpasses = &subpass;
    pass.dependencyCount = 1;
    pass.pDependencies = &dependency;
    if (!check(vkCreateRenderPass(device_, &pass, nullptr, &renderPass_), "vkCreateRenderPass")) return false;

    if (!check(vkGetSwapchainImagesKHR(device_, swapchain_, &count, nullptr), "swapchain images")) return false;
    std::vector<VkImage> swapImages(count);
    if (!check(vkGetSwapchainImagesKHR(device_, swapchain_, &count, swapImages.data()), "swapchain images"))
        return false;
    views_.assign(count, VK_NULL_HANDLE);
    framebuffers_.assign(count, VK_NULL_HANDLE);
    renderDone_.assign(count, VK_NULL_HANDLE);
    for (uint32_t i = 0; i < count; ++i) {
        VkImageViewCreateInfo view{};
        view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view.image = swapImages[i];
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = format_;
        view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        if (!check(vkCreateImageView(device_, &view, nullptr, &views_[i]), "vkCreateImageView")) return false;
        VkFramebufferCreateInfo frame{};
        frame.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        frame.renderPass = renderPass_;
        frame.attachmentCount = 1;
        frame.pAttachments = &views_[i];
        frame.width = extent_.width;
        frame.height = extent_.height;
        frame.layers = 1;
        if (!check(vkCreateFramebuffer(device_, &frame, nullptr, &framebuffers_[i]), "vkCreateFramebuffer"))
            return false;
        VkSemaphoreCreateInfo semaphore{};
        semaphore.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        if (!check(vkCreateSemaphore(device_, &semaphore, nullptr, &renderDone_[i]), "vkCreateSemaphore"))
            return false;
    }
    logf(false, "swapchain ready: %ux%u, %u images, format %d", extent_.width, extent_.height, count,
         static_cast<int>(format_));
    return true;
}

void VulkanRenderer::destroySwapchain() {
    for (VkSemaphore s : renderDone_) vkDestroySemaphore(device_, s, nullptr);
    for (VkFramebuffer f : framebuffers_) vkDestroyFramebuffer(device_, f, nullptr);
    for (VkImageView v : views_) vkDestroyImageView(device_, v, nullptr);
    renderDone_.clear();
    framebuffers_.clear();
    views_.clear();
    if (renderPass_ != VK_NULL_HANDLE) vkDestroyRenderPass(device_, renderPass_, nullptr);
    if (swapchain_ != VK_NULL_HANDLE) vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    renderPass_ = VK_NULL_HANDLE;
    swapchain_ = VK_NULL_HANDLE;
}

bool VulkanRenderer::recreateSwapchain() {
    if (!check(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle")) return false;
    destroySwapchain();
    return createSwapchain();
}

bool VulkanRenderer::record(uint32_t imageIndex, float seconds) {
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (!check(vkResetCommandBuffer(commands_, 0), "reset command buffer") ||
        !check(vkBeginCommandBuffer(commands_, &begin), "begin command buffer"))
        return false;

    VkClearValue background{};
    background.color = {{0.015f, 0.035f, 0.060f, 1.0f}};
    VkRenderPassBeginInfo pass{};
    pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    pass.renderPass = renderPass_;
    pass.framebuffer = framebuffers_[imageIndex];
    pass.renderArea.extent = extent_;
    pass.clearValueCount = 1;
    pass.pClearValues = &background;
    vkCmdBeginRenderPass(commands_, &pass, VK_SUBPASS_CONTENTS_INLINE);

    auto fill = [this](int32_t x, int32_t y, uint32_t w, uint32_t h, float r, float g, float b) {
        if (w == 0 || h == 0) return;
        VkClearAttachment attachment{};
        attachment.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        attachment.clearValue.color = {{r, g, b, 1.0f}};
        VkClearRect rect{{{x, y}, {w, h}}, 0, 1};
        vkCmdClearAttachments(commands_, 1, &attachment, 1, &rect);
    };

    // One block per boot stage along the bottom: gold means that stage really completed.
    const uint32_t margin = extent_.width / 10;
    const uint32_t slot = (extent_.width - 2 * margin) / kBootStages;
    const uint32_t blockHeight = std::max(extent_.height / 40, 1u);
    const int32_t top = static_cast<int32_t>(extent_.height - 4 * blockHeight);
    const int done = stagesDone_.load();
    for (int i = 0; i < kBootStages; ++i) {
        const bool lit = i < done;
        fill(static_cast<int32_t>(margin + i * slot), top, slot * 9 / 10, blockHeight,
             lit ? 0.80f : 0.12f, lit ? 0.62f : 0.16f, lit ? 0.28f : 0.21f);
    }
    // A moving marker shows that new frames are being presented.
    const uint32_t marker = std::max(extent_.width / 12, 1u);
    const float phase = 0.5f - 0.5f * std::cos(seconds * 2.0f);
    fill(static_cast<int32_t>(margin + phase * static_cast<float>(extent_.width - 2 * margin - marker)),
         top + static_cast<int32_t>(2 * blockHeight), marker, std::max(blockHeight / 3, 1u),
         0.42f, 0.73f, 0.88f);

    vkCmdEndRenderPass(commands_);
    return check(vkEndCommandBuffer(commands_), "end command buffer");
}

bool VulkanRenderer::drawFrame(float seconds) {
    if (device_ == VK_NULL_HANDLE) return false;
    if (swapchain_ == VK_NULL_HANDLE) return createSwapchain();
    if (!check(vkWaitForFences(device_, 1, &frameDone_, VK_TRUE, kFenceTimeoutNs), "vkWaitForFences"))
        return false;

    uint32_t index = 0;
    const VkResult acquired = vkAcquireNextImageKHR(device_, swapchain_, kAcquireTimeoutNs, imageReady_,
                                                    VK_NULL_HANDLE, &index);
    if (acquired == VK_TIMEOUT || acquired == VK_NOT_READY) return true;
    if (acquired == VK_ERROR_OUT_OF_DATE_KHR) return recreateSwapchain();
    if (acquired != VK_SUBOPTIMAL_KHR && !check(acquired, "vkAcquireNextImageKHR")) return false;

    if (!record(index, seconds)) return false;
    const VkPipelineStageFlags wait = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &imageReady_;
    submit.pWaitDstStageMask = &wait;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &commands_;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &renderDone_[index];
    // Reset only when work is certain to be submitted, so a skipped frame cannot deadlock.
    if (!check(vkResetFences(device_, 1, &frameDone_), "vkResetFences") ||
        !check(vkQueueSubmit(queue_, 1, &submit, frameDone_), "vkQueueSubmit"))
        return false;

    VkPresentInfoKHR present{};
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &renderDone_[index];
    present.swapchainCount = 1;
    present.pSwapchains = &swapchain_;
    present.pImageIndices = &index;
    const VkResult shown = vkQueuePresentKHR(queue_, &present);
    if (shown == VK_ERROR_OUT_OF_DATE_KHR) return recreateSwapchain();
    if (shown != VK_SUBOPTIMAL_KHR && !check(shown, "vkQueuePresentKHR")) return false;
    if (++presented_ == 1) logf(false, "first frame presented");

    // SUBOPTIMAL can persist on rotated Android displays; rebuild only if the size changed.
    if (shown == VK_SUBOPTIMAL_KHR || acquired == VK_SUBOPTIMAL_KHR) {
        VkSurfaceCapabilitiesKHR caps{};
        if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_, surface_, &caps) == VK_SUCCESS &&
            caps.currentExtent.width != UINT32_MAX &&
            (caps.currentExtent.width != extent_.width || caps.currentExtent.height != extent_.height))
            return recreateSwapchain();
    }
    return true;
}

void VulkanRenderer::destroy() {
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
        destroySwapchain();
        if (frameDone_ != VK_NULL_HANDLE) vkDestroyFence(device_, frameDone_, nullptr);
        if (imageReady_ != VK_NULL_HANDLE) vkDestroySemaphore(device_, imageReady_, nullptr);
        if (pool_ != VK_NULL_HANDLE) vkDestroyCommandPool(device_, pool_, nullptr);
        vkDestroyDevice(device_, nullptr);
    }
    if (surface_ != VK_NULL_HANDLE) vkDestroySurfaceKHR(instance_, surface_, nullptr);
    if (instance_ != VK_NULL_HANDLE) vkDestroyInstance(instance_, nullptr);
    frameDone_ = VK_NULL_HANDLE;
    imageReady_ = VK_NULL_HANDLE;
    pool_ = VK_NULL_HANDLE;
    commands_ = VK_NULL_HANDLE;
    device_ = VK_NULL_HANDLE;
    queue_ = VK_NULL_HANDLE;
    physical_ = VK_NULL_HANDLE;
    surface_ = VK_NULL_HANDLE;
    instance_ = VK_NULL_HANDLE;
}

}  // namespace rottr
