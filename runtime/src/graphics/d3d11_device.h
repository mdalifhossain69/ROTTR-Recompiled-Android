#pragma once

#include "graphics/dxgi_vk_format.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace rottr::gfx::d3d11 {

enum class ResourceKind : uint8_t { Buffer, Texture2D };
enum class Usage : uint8_t { Default, Immutable, Dynamic, Staging };
enum Bind : uint32_t {
    BindVertexBuffer = 1U << 0U, BindIndexBuffer = 1U << 1U,
    BindConstantBuffer = 1U << 2U, BindShaderResource = 1U << 3U,
    BindRenderTarget = 1U << 4U, BindDepthStencil = 1U << 5U,
    BindUnorderedAccess = 1U << 6U
};
enum CpuAccess : uint32_t { CpuRead = 1U, CpuWrite = 2U };
enum class MapMode : uint8_t { Read, Write, ReadWrite, WriteDiscard, WriteNoOverwrite };
enum class IndexFormat : uint8_t { UInt16, UInt32 };
enum class PrimitiveTopology : uint8_t { Undefined, PointList, LineList, LineStrip, TriangleList, TriangleStrip };

struct BufferDesc {
    uint32_t byteWidth = 0;
    Usage usage = Usage::Default;
    uint32_t bindFlags = 0;
    uint32_t cpuAccessFlags = 0;
    uint32_t structureByteStride = 0;
};
struct Texture2DDesc {
    uint32_t width = 0, height = 0, mipLevels = 1, arraySize = 1;
    DxgiFormat format = DxgiFormat::Unknown;
    uint32_t sampleCount = 1;
    Usage usage = Usage::Default;
    uint32_t bindFlags = 0, cpuAccessFlags = 0;
};
struct SubresourceData { const void* data = nullptr; uint32_t rowPitch = 0; uint32_t slicePitch = 0; };
struct MappedSubresource { void* data = nullptr; uint32_t rowPitch = 0; uint32_t depthPitch = 0; };

struct Resource {
    ResourceKind kind = ResourceKind::Buffer;
    uint64_t id = 0;
    BufferDesc buffer{};
    Texture2DDesc texture{};
    std::vector<uint8_t> hostData;
    uint32_t mapCount = 0;
    bool alive = true;
};
using ResourcePtr = std::shared_ptr<Resource>;

struct DrawCommand {
    enum class Type : uint8_t { Draw, DrawIndexed, UpdateBuffer, CopyResource, ClearState };
    Type type = Type::Draw;
    uint32_t a = 0, b = 0, c = 0, d = 0;
    uint64_t resource = 0, otherResource = 0;
};

class Backend {
public:
    virtual ~Backend() = default;
    virtual bool createBuffer(const BufferDesc&, const void*, uint64_t& handle) = 0;
    virtual bool createTexture2D(const Texture2DDesc&, const SubresourceData*, uint64_t& handle) = 0;
    virtual void destroyResource(uint64_t handle) = 0;
    virtual bool uploadBuffer(uint64_t handle, uint32_t offset, const void*, uint32_t size) = 0;
    virtual bool copyResource(uint64_t dst, uint64_t src) = 0;
    virtual bool bindGraphicsPipeline(uint64_t) { return false; }
    virtual bool bindVertexBuffer(uint32_t, uint64_t, uint32_t, uint32_t) { return false; }
    virtual bool bindIndexBuffer(uint64_t, IndexFormat, uint32_t) { return false; }
    virtual bool draw(uint32_t vertexCount, uint32_t startVertex,
                      uint32_t instanceCount, uint32_t startInstance) = 0;
    virtual bool drawIndexed(uint32_t indexCount, uint32_t startIndex,
                             int32_t baseVertex, uint32_t instanceCount) = 0;
};

class Device {
public:
    explicit Device(std::shared_ptr<Backend> backend);
    ~Device();
    bool createBuffer(const BufferDesc&, const SubresourceData*, ResourcePtr& out);
    bool createTexture2D(const Texture2DDesc&, const SubresourceData*, ResourcePtr& out);
    bool copyResource(const ResourcePtr& dst, const ResourcePtr& src);
    bool updateBuffer(const ResourcePtr&, uint32_t offset, const void*, uint32_t size);
    bool map(const ResourcePtr&, MapMode, MappedSubresource&);
    bool unmap(const ResourcePtr&);
    bool valid() const;
    std::shared_ptr<Backend> backend() const { return backend_; }

private:
    bool validate(const BufferDesc&) const;
    bool validate(const Texture2DDesc&, const SubresourceData*) const;
    std::shared_ptr<Backend> backend_;
    mutable std::mutex mutex_;
    std::vector<ResourcePtr> resources_;
};

class ImmediateContext {
public:
    explicit ImmediateContext(Device& device);
    void setVertexBuffer(uint32_t slot, ResourcePtr buffer, uint32_t stride, uint32_t offset);
    void setIndexBuffer(ResourcePtr buffer, IndexFormat format, uint32_t offset);
    void setConstantBuffer(uint32_t slot, ResourcePtr buffer);
    void setShaderResource(uint32_t slot, ResourcePtr texture);
    void setRenderTarget(ResourcePtr texture);
    void setTopology(PrimitiveTopology topology);
    bool setGraphicsPipeline(uint64_t pipeline);
    bool draw(uint32_t vertexCount, uint32_t startVertex = 0,
              uint32_t instanceCount = 1, uint32_t startInstance = 0);
    bool drawIndexed(uint32_t indexCount, uint32_t startIndex = 0,
                     int32_t baseVertex = 0, uint32_t instanceCount = 1);
    bool updateSubresource(const ResourcePtr&, uint32_t offset, const void*, uint32_t size);
    bool copyResource(const ResourcePtr& dst, const ResourcePtr& src);
    void clearState();
    std::vector<DrawCommand> takeCommands();
    uint64_t drawCalls() const;

private:
    bool hasVertexBuffer() const;
    Device& device_;
    mutable std::mutex mutex_;
    std::array<ResourcePtr, 16> vertexBuffers_{};
    std::array<uint32_t, 16> vertexStrides_{};
    std::array<uint32_t, 16> vertexOffsets_{};
    std::array<ResourcePtr, 16> constantBuffers_{};
    std::array<ResourcePtr, 128> shaderResources_{};
    ResourcePtr indexBuffer_;
    ResourcePtr renderTarget_;
    IndexFormat indexFormat_ = IndexFormat::UInt16;
    uint32_t indexOffset_ = 0;
    PrimitiveTopology topology_ = PrimitiveTopology::Undefined;
    uint64_t graphicsPipeline_ = 0;
    std::vector<DrawCommand> commands_;
    uint64_t drawCalls_ = 0;
};

} // namespace rottr::gfx::d3d11
