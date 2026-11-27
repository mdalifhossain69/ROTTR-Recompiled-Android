#include "graphics/d3d11_device.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>

using namespace rottr::gfx;
using namespace rottr::gfx::d3d11;

class FakeBackend final : public Backend {
public:
    uint64_t next = 1;
    uint32_t draws = 0, indexed = 0, uploads = 0, copies = 0, destroys = 0, pipelineBinds = 0, vertexBinds = 0, indexBinds = 0;
    bool createBuffer(const BufferDesc&, const void*, uint64_t& h) override { h = next++; return true; }
    bool createTexture2D(const Texture2DDesc&, const SubresourceData*, uint64_t& h) override { h = next++; return true; }
    void destroyResource(uint64_t) override { ++destroys; }
    bool uploadBuffer(uint64_t, uint32_t, const void*, uint32_t) override { ++uploads; return true; }
    bool copyResource(uint64_t, uint64_t) override { ++copies; return true; }
    bool bindGraphicsPipeline(uint64_t p) override { ++pipelineBinds; return p != 0; }
    bool bindVertexBuffer(uint32_t, uint64_t, uint32_t, uint32_t) override { ++vertexBinds; return true; }
    bool bindIndexBuffer(uint64_t, IndexFormat, uint32_t) override { ++indexBinds; return true; }
    bool draw(uint32_t, uint32_t, uint32_t, uint32_t) override { ++draws; return true; }
    bool drawIndexed(uint32_t, uint32_t, int32_t, uint32_t) override { ++indexed; return true; }
};

int main() {
    auto backend = std::make_shared<FakeBackend>();
    {
        Device device(backend);
        assert(device.valid());
        BufferDesc vb{256, Usage::Default, BindVertexBuffer, 0, 0};
        BufferDesc ib{128, Usage::Default, BindIndexBuffer, 0, 0};
        BufferDesc cb{64, Usage::Default, BindConstantBuffer, 0, 0};
        ResourcePtr vertex, index, constant;
        assert(device.createBuffer(vb, nullptr, vertex));
        assert(device.createBuffer(ib, nullptr, index));
        assert(device.createBuffer(cb, nullptr, constant));
        assert(vertex->id != index->id);

        ImmediateContext context(device);
        assert(!context.draw(3));
        context.setTopology(PrimitiveTopology::TriangleList);
        assert(context.setGraphicsPipeline(99));
        context.setVertexBuffer(0, vertex, 16, 0);
        context.setConstantBuffer(0, constant);
        assert(context.draw(3));
        context.setIndexBuffer(index, IndexFormat::UInt16, 0);
        assert(context.drawIndexed(6));
        assert(!context.drawIndexed(1000));
        assert(context.drawCalls() == 2);
        assert(backend->draws == 1 && backend->indexed == 1);
        assert(backend->pipelineBinds == 1 && backend->vertexBinds == 2 && backend->indexBinds == 1);

        uint8_t bytes[8] = {1,2,3,4,5,6,7,8};
        assert(context.updateSubresource(vertex, 4, bytes, sizeof(bytes)));
        assert(std::memcmp(vertex->hostData.data() + 4, bytes, sizeof(bytes)) == 0);
        assert(!context.updateSubresource(vertex, 252, bytes, sizeof(bytes)));
        assert(backend->uploads == 1);

        ResourcePtr clone;
        assert(device.createBuffer(vb, nullptr, clone));
        assert(context.copyResource(clone, vertex));
        assert(backend->copies == 1);

        BufferDesc dynamic{32, Usage::Dynamic, BindVertexBuffer, CpuWrite, 0};
        ResourcePtr mappedBuffer;
        assert(device.createBuffer(dynamic, nullptr, mappedBuffer));
        MappedSubresource mapped{};
        assert(device.map(mappedBuffer, MapMode::WriteDiscard, mapped));
        assert(mapped.data != nullptr && mapped.rowPitch == 32);
        static_cast<uint8_t*>(mapped.data)[0] = 42;
        assert(!device.map(mappedBuffer, MapMode::Write, mapped));
        assert(device.unmap(mappedBuffer));
        assert(backend->uploads == 2);
        assert(!device.unmap(mappedBuffer));

        BufferDesc invalid{0, Usage::Default, BindVertexBuffer, 0, 0};
        ResourcePtr rejected;
        assert(!device.createBuffer(invalid, nullptr, rejected));
        assert(!rejected);

        Texture2DDesc texture{64,64,1,1,DxgiFormat::BC1Unorm,1,Usage::Default,BindShaderResource,0};
        ResourcePtr image;
        assert(device.createTexture2D(texture, nullptr, image));
        assert(image->kind == ResourceKind::Texture2D);
        Texture2DDesc badTexture = texture;
        badTexture.format = DxgiFormat::Unknown;
        assert(!device.createTexture2D(badTexture, nullptr, rejected));

        context.clearState();
        assert(!context.draw(3));
        assert(!context.takeCommands().empty());
    }
    assert(backend->destroys == 6);
    return 0;
}
