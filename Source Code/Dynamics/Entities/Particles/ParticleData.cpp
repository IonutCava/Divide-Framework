#include "stdafx.h"

#include "Headers/ParticleData.h"

#include "Core/Headers/Kernel.h"
#include "Core/Headers/PlatformContext.h"
#include "Platform/Video/Headers/GFXDevice.h"

namespace Divide {

ParticleData::ParticleData(GFXDevice& context, const U32 particleCount, const U32 optionsMask)
    : _context(context)
{
    _isBillboarded = true;
    // Default particles are quad sprites.
    // To optimise fillrate we could provide geometry that tightly fits the particle's texture
    _particleGeometryVertices.resize(4);
    _particleGeometryVertices[0].set(-0.5f,  0.5f, 0.0f);
    _particleGeometryVertices[1].set(-0.5f, -0.5f, 0.0f);
    _particleGeometryVertices[2].set(0.5f,  0.5f, 0.0f);
    _particleGeometryVertices[3].set(0.5f, -0.5f, 0.0f);
    _particleGeometryType = PrimitiveTopology::TRIANGLE_STRIP;
    generateParticles(particleCount, optionsMask);
}

ParticleData::~ParticleData()
{
}

void ParticleData::generateParticles(const U32 particleCount, const U32 optionsMask) {
    _totalCount = particleCount;
    _aliveCount = 0;
    _optionsMask = optionsMask;

    _indices.clear();
    _position.clear();
    _velocity.clear();
    _acceleration.clear();
    _misc.clear();
    _colour.clear();
    _startColour.clear();
    _endColour.clear();

    if (_totalCount > 0) {
        if (TestBit(_optionsMask, to_U32(ParticleDataProperties::PROPERTIES_POS))) {
            _position.resize(_totalCount, vec4<F32>(0.0f));
        }
        if (TestBit(_optionsMask, to_U32(ParticleDataProperties::PROPERTIES_VEL))) {
            _velocity.resize(_totalCount, vec4<F32>(0.0f));
        }
        if (TestBit(_optionsMask, to_U32(ParticleDataProperties::PROPERTIES_ACC))) {
            _acceleration.resize(_totalCount, vec4<F32>(0.0f));
        }
        if (TestBit(_optionsMask, to_U32(ParticleDataProperties::PROPERTIES_COLOR))) {
            _colour.resize(_totalCount, FColour4(0.0f));
        }
        if (TestBit(_optionsMask, to_U32(ParticleDataProperties::PROPERTIES_COLOR_TRANS))) {
            _startColour.resize(_totalCount, FColour4(0.0f));
            _endColour.resize(_totalCount, FColour4(0.0f));
        }
        _misc.resize(_totalCount, vec4<F32>(0.0f));
    }
}

void ParticleData::kill(const U32 index) {
    swapData(index, _aliveCount - 1);
    _aliveCount--;
}

void ParticleData::wake(U32 /*index*/) {
    //swapData(index, _aliveCount);
    _aliveCount++;
}

void ParticleData::sort() {
    const U32 count = aliveCount();

    if (count == 0) {
        return;
    }

    _indices.resize(count);
    _renderingPositions.resize(count);
    _renderingColours.resize(count);

    for (U32 i = 0; i < count; ++i) {
        _indices[i] = { i, _misc[i].w };
    }

    eastl::sort(eastl::begin(_indices),
                eastl::end(_indices),
                [](const std::pair<U32, F32>& indexA, const std::pair<U32, F32>& indexB) {
                    return indexA.second > indexB.second;
                });

   auto parsePositions = [count, this](const Task&) -> void {
        for (U32 i = 0; i < count; ++i) {
            _renderingPositions[i].set(_position[_indices[i].first]);
        }
    };

    auto parseColours = [count, this](const Task&) -> void {
        for (U32 i = 0; i < count; ++i) {
            Util::ToByteColour(_colour[_indices[i].first], _renderingColours[i]);
        }
    };
    
    TaskPool& pool = _context.context().taskPool(TaskPoolType::HIGH_PRIORITY);
    Task* updateTask = CreateTask(TASK_NOP);
    Start(*CreateTask(updateTask, parsePositions), pool);
    Start(*CreateTask(updateTask, parseColours), pool);
    StartAndWait(*updateTask, pool);
}

void ParticleData::swapData(const U32 indexA, const U32 indexB) {
    if (TestBit(_optionsMask, to_U32(ParticleDataProperties::PROPERTIES_POS))) {
        _position[indexA].set(_position[indexB]);
    }
    if (TestBit(_optionsMask, to_U32(ParticleDataProperties::PROPERTIES_VEL))) {
        _velocity[indexA].set(_velocity[indexB]);
    }
    if (TestBit(_optionsMask, to_U32(ParticleDataProperties::PROPERTIES_ACC))) {
        _acceleration[indexA].set(_acceleration[indexB]);
    }
    if (TestBit(_optionsMask, to_U32(ParticleDataProperties::PROPERTIES_COLOR))) {
        _colour[indexA].set(_colour[indexB]);
    }
    if (TestBit(_optionsMask, to_U32(ParticleDataProperties::PROPERTIES_COLOR_TRANS))) {
        _startColour[indexA].set(_startColour[indexB]);
        _endColour[indexA].set(_endColour[indexB]);
    }
    _misc[indexA].set(_misc[indexB]);
}

void ParticleData::setParticleGeometry(const vector<vec3<F32>>& particleGeometryVertices,
                                       const vector<U32>& particleGeometryIndices,
                                       const PrimitiveTopology particleGeometryType) {
    _particleGeometryVertices = particleGeometryVertices;
    _particleGeometryIndices = particleGeometryIndices;
    _particleGeometryType = particleGeometryType;
}

void ParticleData::setBillboarded(const bool state) noexcept {
    _isBillboarded = state;
}
}