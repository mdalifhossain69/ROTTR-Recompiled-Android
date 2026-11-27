#include "graphics/d3d11_device.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace rottr::gfx::d3d11 {
namespace {
bool hasFlag(uint32_t value, uint32_t flag) { return (value & flag) != 0U; }
bool sameShape(const Resource& a, const Resource& b) {
    if (a.kind != b.kind) return false;
    if (a.kind == ResourceKind::Buffer) return a.buffer.byteWidth == b.buffer.byteWidth;
    return a.texture.width == b.texture.width && a.texture.height == b.texture.height &&
           a.texture.mipLevels == b.texture.mipLevels && a.texture.arraySize == b.texture.arraySize &&
           a.texture.format == b.texture.format && a.texture.sampleCount == b.texture.sampleCount;
}
}

Device::Device(std::shared_ptr<Backend> backend) : backend_(std::move(backend)) {}
Device::~Device() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& resource : resources_) if (resource) {
        if (resource->alive && backend_) backend_->destroyResource(resource->id);
        resource->alive = false;
    }
}
bool Device::valid() const { return static_cast<bool>(backend_); }

bool Device::validate(const BufferDesc& d) const {
    if (!backend_ || d.byteWidth == 0 || d.usage > Usage::Staging) return false;
    if (d.usage == Usage::Immutable && d.cpuAccessFlags != 0) return false;
    if (d.usage == Usage::Dynamic && !hasFlag(d.cpuAccessFlags, CpuWrite)) return false;
    if (d.usage == Usage::Staging && d.bindFlags != 0) return false;
    if (hasFlag(d.bindFlags, BindConstantBuffer) && (d.byteWidth % 16U) != 0U) return false;
    return true;
}
bool Device::validate(const Texture2DDesc& d, const SubresourceData* initial) const {
    if (!backend_ || d.width == 0 || d.height == 0 || d.mipLevels == 0 || d.arraySize == 0 ||
        d.sampleCount == 0 || !isFormatSupported(d.format)) return false;
    if (d.sampleCount > 1 && d.mipLevels != 1) return false;
    if (d.usage == Usage::Immutable && initial == nullptr) return false;
    if (d.usage == Usage::Dynamic && !hasFlag(d.cpuAccessFlags, CpuWrite)) return false;
    if (d.usage == Usage::Staging && d.bindFlags != 0) return false;
    if (d.sampleCount > 1 && initial != nullptr) return false;
    return true;
}
bool Device::createBuffer(const BufferDesc& desc, const SubresourceData* initial, ResourcePtr& out) {
    out.reset();
    if (!validate(desc) || (desc.usage == Usage::Immutable && (!initial || !initial->data))) return false;
    uint64_t handle = 0;
    if (!backend_->createBuffer(desc, initial ? initial->data : nullptr, handle) || handle == 0) return false;
    auto resource = std::make_shared<Resource>();
    resource->kind = ResourceKind::Buffer; resource->buffer = desc; resource->id = handle;
    try {
        if (initial && initial->data) {
            const auto* bytes = static_cast<const uint8_t*>(initial->data);
            resource->hostData.assign(bytes, bytes + desc.byteWidth);
        } else resource->hostData.resize(desc.byteWidth);
    } catch (...) { backend_->destroyResource(handle); return false; }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        resources_.push_back(resource);
    }
    out = std::move(resource);
    return true;
}
bool Device::createTexture2D(const Texture2DDesc& desc, const SubresourceData* initial, ResourcePtr& out) {
    out.reset();
    if (!validate(desc, initial)) return false;
    uint64_t handle = 0;
    if (!backend_->createTexture2D(desc, initial, handle) || handle == 0) return false;
    auto resource = std::make_shared<Resource>();
    resource->kind = ResourceKind::Texture2D; resource->texture = desc; resource->id = handle;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        resources_.push_back(resource);
    }
    out = std::move(resource);
    return true;
}
bool Device::copyResource(const ResourcePtr& dst, const ResourcePtr& src) {
    if (!dst || !src || !dst->alive || !src->alive || dst == src || !sameShape(*dst, *src)) return false;
    return backend_->copyResource(dst->id, src->id);
}
bool Device::updateBuffer(const ResourcePtr& resource, uint32_t offset, const void* data, uint32_t size) {
    if (!resource || !resource->alive || resource->kind != ResourceKind::Buffer || !data ||
        offset > resource->buffer.byteWidth || size > resource->buffer.byteWidth - offset ||
        resource->buffer.usage == Usage::Immutable || resource->buffer.usage == Usage::Staging) return false;
    if (!backend_->uploadBuffer(resource->id, offset, data, size)) return false;
    std::memcpy(resource->hostData.data() + offset, data, size);
    return true;
}
bool Device::map(const ResourcePtr& resource, MapMode mode, MappedSubresource& mapped) {
    mapped = {};
    if (!resource || !resource->alive || resource->kind != ResourceKind::Buffer || resource->mapCount != 0) return false;
    const bool read = mode == MapMode::Read || mode == MapMode::ReadWrite;
    const bool write = mode != MapMode::Read;
    if (read && !hasFlag(resource->buffer.cpuAccessFlags, CpuRead)) return false;
    if (write && !hasFlag(resource->buffer.cpuAccessFlags, CpuWrite)) return false;
    if (resource->buffer.usage == Usage::Immutable || resource->buffer.usage == Usage::Default) return false;
    if (mode == MapMode::WriteDiscard) std::fill(resource->hostData.begin(), resource->hostData.end(), 0);
    resource->mapCount = 1;
    mapped.data = resource->hostData.data();
    mapped.rowPitch = resource->buffer.byteWidth;
    mapped.depthPitch = resource->buffer.byteWidth;
    return true;
}
bool Device::unmap(const ResourcePtr& resource) {
    if (!resource || !resource->alive || resource->mapCount != 1) return false;
    if (hasFlag(resource->buffer.cpuAccessFlags, CpuWrite) &&
        !backend_->uploadBuffer(resource->id, 0, resource->hostData.data(),
                                static_cast<uint32_t>(resource->hostData.size()))) return false;
    resource->mapCount = 0;
    return true;
}

ImmediateContext::ImmediateContext(Device& device) : device_(device) {}
void ImmediateContext::setVertexBuffer(uint32_t slot, ResourcePtr b, uint32_t stride, uint32_t offset) {
    if (slot >= vertexBuffers_.size() || (b && (b->kind != ResourceKind::Buffer ||
        !hasFlag(b->buffer.bindFlags, BindVertexBuffer) || offset >= b->buffer.byteWidth))) return;
    std::lock_guard<std::mutex> lock(mutex_);
    vertexBuffers_[slot] = std::move(b); vertexStrides_[slot] = stride; vertexOffsets_[slot] = offset;
}
void ImmediateContext::setIndexBuffer(ResourcePtr b, IndexFormat format, uint32_t offset) {
    if (b && (b->kind != ResourceKind::Buffer || !hasFlag(b->buffer.bindFlags, BindIndexBuffer) ||
        offset >= b->buffer.byteWidth || (offset % (format == IndexFormat::UInt16 ? 2U : 4U)) != 0U)) return;
    std::lock_guard<std::mutex> lock(mutex_);
    indexBuffer_ = std::move(b); indexFormat_ = format; indexOffset_ = offset;
}
void ImmediateContext::setConstantBuffer(uint32_t slot, ResourcePtr b) {
    if (slot >= constantBuffers_.size() || (b && (b->kind != ResourceKind::Buffer ||
        !hasFlag(b->buffer.bindFlags, BindConstantBuffer)))) return;
    std::lock_guard<std::mutex> lock(mutex_); constantBuffers_[slot] = std::move(b);
}
void ImmediateContext::setShaderResource(uint32_t slot, ResourcePtr r) {
    if (slot >= shaderResources_.size() || (r && (r->kind != ResourceKind::Texture2D ||
        !hasFlag(r->texture.bindFlags, BindShaderResource)))) return;
    std::lock_guard<std::mutex> lock(mutex_); shaderResources_[slot] = std::move(r);
}
void ImmediateContext::setRenderTarget(ResourcePtr r) {
    if (r && (r->kind != ResourceKind::Texture2D || !hasFlag(r->texture.bindFlags, BindRenderTarget))) return;
    std::lock_guard<std::mutex> lock(mutex_); renderTarget_ = std::move(r);
}
void ImmediateContext::setTopology(PrimitiveTopology t) {
    std::lock_guard<std::mutex> lock(mutex_); topology_ = t;
}
bool ImmediateContext::hasVertexBuffer() const {
    return std::any_of(vertexBuffers_.begin(), vertexBuffers_.end(), [](const ResourcePtr& r) {
        return r && r->alive;
    });
}
bool ImmediateContext::draw(uint32_t count, uint32_t start, uint32_t instances, uint32_t firstInstance) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!device_.valid() || count == 0 || instances == 0 || topology_ == PrimitiveTopology::Undefined ||
        !hasVertexBuffer()) return false;
    if (!device_.backend()->draw(count, start, instances, firstInstance)) return false;
    DrawCommand c; c.type = DrawCommand::Type::Draw; c.a = count; c.b = start; c.c = instances; c.d = firstInstance;
    commands_.push_back(c); ++drawCalls_; return true;
}
bool ImmediateContext::drawIndexed(uint32_t count, uint32_t start, int32_t base, uint32_t instances) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!device_.valid() || count == 0 || instances == 0 || topology_ == PrimitiveTopology::Undefined ||
        !hasVertexBuffer() || !indexBuffer_ || !indexBuffer_->alive) return false;
    const uint64_t bytes = static_cast<uint64_t>(count) * (indexFormat_ == IndexFormat::UInt16 ? 2U : 4U);
    if (static_cast<uint64_t>(indexOffset_) + static_cast<uint64_t>(start) *
        (indexFormat_ == IndexFormat::UInt16 ? 2U : 4U) + bytes > indexBuffer_->buffer.byteWidth) return false;
    if (!device_.backend()->drawIndexed(count, start, base, instances)) return false;
    DrawCommand c; c.type = DrawCommand::Type::DrawIndexed; c.a = count; c.b = start;
    c.c = static_cast<uint32_t>(base); c.d = instances; c.resource = indexBuffer_->id;
    commands_.push_back(c); ++drawCalls_; return true;
}
bool ImmediateContext::updateSubresource(const ResourcePtr& r, uint32_t offset, const void* data, uint32_t size) {
    if (!device_.updateBuffer(r, offset, data, size)) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    DrawCommand c; c.type = DrawCommand::Type::UpdateBuffer; c.a = offset; c.b = size; c.resource = r->id;
    commands_.push_back(c); return true;
}
bool ImmediateContext::copyResource(const ResourcePtr& dst, const ResourcePtr& src) {
    if (!device_.copyResource(dst, src)) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    DrawCommand c; c.type = DrawCommand::Type::CopyResource; c.resource = dst->id; c.otherResource = src->id;
    commands_.push_back(c); return true;
}
void ImmediateContext::clearState() {
    std::lock_guard<std::mutex> lock(mutex_);
    vertexBuffers_.fill(nullptr); constantBuffers_.fill(nullptr); shaderResources_.fill(nullptr);
    indexBuffer_.reset(); renderTarget_.reset(); topology_ = PrimitiveTopology::Undefined;
    DrawCommand c; c.type = DrawCommand::Type::ClearState; commands_.push_back(c);
}
std::vector<DrawCommand> ImmediateContext::takeCommands() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<DrawCommand> result; result.swap(commands_); return result;
}
uint64_t ImmediateContext::drawCalls() const {
    std::lock_guard<std::mutex> lock(mutex_); return drawCalls_;
}
} // namespace rottr::gfx::d3d11
