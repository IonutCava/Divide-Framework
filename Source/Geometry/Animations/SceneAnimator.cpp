

#include "Headers/SceneAnimator.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Headers/AnimationUtils.h"

#include "Core/Headers/PlatformContext.h"
#include "Core/Headers/ByteBuffer.h"
#include "Utility/Headers/Localization.h"

namespace Divide
{
    constexpr U16 BYTE_BUFFER_VERSION_ANIMATOR = 1u;
    constexpr U16 BYTE_BUFFER_VERSION_SKELETON = 2u;

namespace
{
    mat4<F32> GetBoneGlobalTransform(Bone& bone)
    {
        if (bone.parent() != nullptr)
        {
            return bone._localTransform * GetBoneGlobalTransform(*bone.parent());
        }

        return bone._localTransform;
    }

    I32 CreateSkeleton(const Bone_uptr& piNode,
                       const mat4<F32>& parent,
                       vector<Line>& lines)
    {
        static Line s_line
        {
            ._positionStart = VECTOR3_ZERO,
            ._positionEnd = VECTOR3_UNIT,
            ._colourStart = DefaultColours::RED_U8,
            ._colourEnd = DefaultColours::RED_U8,
            ._widthStart = 2.0f,
            ._widthEnd = 2.0f
        };

        const mat4<F32>& me = GetBoneGlobalTransform(*piNode);

        if (piNode->parent())
        {
            Line& line = lines.emplace_back(s_line);
            line._positionStart = parent.getRow(3).xyz;
            line._positionEnd = me.getRow(3).xyz;
        }

        // render all child nodes
        for (const Bone_uptr& bone : piNode->children())
        {
            CreateSkeleton(bone, me, lines);
        }

        return 1;
    }
}

SceneAnimator::SceneAnimator(const bool useDualQuaternion)
    : _useDualQuaternion(useDualQuaternion)
{
}

SceneAnimator::~SceneAnimator()
{
    release(true);
}

void SceneAnimator::release(const bool releaseAnimations)
{
    _skeleton.reset();

    // this should clean everything up
    _skeletonLines.clear();
    _skeletonLinesContainer.clear();
    _skeletonDepthCache = U8_ZERO;

    if (releaseAnimations)
    {
        // clear all animations
        _animationNameToID.clear();
        _animations.clear();
    }
}

void UpdateTransformContainer(const Bone_uptr& node, BoneMatrices& containerOut)
{
    if ( node != nullptr)
    {
        if ( node->_boneID != Bone::INVALID_BONE_IDX)
        {
            containerOut[node->_boneID] = node->_offsetMatrix * GetBoneGlobalTransform(*node);
        }

        for (const Bone_uptr& child :  node->children())
        {
            UpdateTransformContainer(child, containerOut);
        }
    }
}

bool SceneAnimator::init([[maybe_unused]] PlatformContext& context)
{
    Console::d_printfn(LOCALE_STR("LOAD_ANIMATIONS_BEGIN"));

    constexpr D64 timeStep = 1. / ANIMATION_TICKS_PER_SECOND;

    const U32 animationCount = to_U32(_animations.size());
    _skeletonLines.resize(animationCount);

    // pre-calculate the animations
    for (U32 i = 0u; i < animationCount; ++i)
    {
        AnimEvaluator* crtAnimation = _animations[i].get();
        const D64 duration = crtAnimation->duration();
        const D64 tickStep = crtAnimation->ticksPerSecond() / ANIMATION_TICKS_PER_SECOND;
        D64 dt = 0;
        U32 frameCount = 0u;
        for (D64 ticks = 0; ticks < duration; ticks += tickStep)
        {
            dt += timeStep;
            calculate(i, dt);

            BoneMatrices& transformMatrices = crtAnimation->transformMatrices().emplace_back();
            transformMatrices.resize(_skeletonDepthCache, MAT4_IDENTITY);
            UpdateTransformContainer(_skeleton, transformMatrices);
            ++frameCount;
        }

        _maximumAnimationFrames = std::max(crtAnimation->frameCount(), _maximumAnimationFrames);
        _skeletonLines[i].resize(_maximumAnimationFrames, -1);

        if ( useDualQuaternion() )
        {
            crtAnimation->transformQuaternions().reserve(crtAnimation->transformMatrices().size());

            for (const BoneMatrices& transformMatrices : crtAnimation->transformMatrices() )
            {
                BoneQuaternions& transformQuaternions = crtAnimation->transformQuaternions().emplace_back();
                transformQuaternions.resize(transformMatrices.size());

                size_t index = 0u;
                for ( const mat4<F32>& matrix : transformMatrices )
                {
                    DualQuaternion& dualQuat = transformQuaternions[index++];
                    Util::ToDualQuaternion(matrix, dualQuat.a, dualQuat.b);
                }
            }
        }

        Attorney::AnimEvaluatorSceneAnimator::frameCount(*crtAnimation, frameCount);
    }

    Console::d_printfn(LOCALE_STR("LOAD_ANIMATIONS_END"), _skeletonDepthCache);

    return _skeletonDepthCache > 0;
}

void SceneAnimator::buildBuffers(GFXDevice& gfxDevice)
{
    // pay the cost upfront
    for (AnimEvaluator_uptr& crtAnimation : _animations)
    {
        if ( !crtAnimation->initBuffers(gfxDevice, useDualQuaternion()) )
        {
            DIVIDE_UNEXPECTED_CALL();
        }
    }
}

/// This will build the skeleton based on the scene passed to it and CLEAR EVERYTHING
bool SceneAnimator::init( PlatformContext& context, Bone_uptr&& skeleton)
{
    release(false);


    _skeleton = MOV(skeleton);
    _skeletonDepthCache = _skeleton->hierarchyDepth();

    DIVIDE_ASSERT(_skeletonDepthCache < U8_MAX, "SceneAnimator::init error: Too many bones for current node!");

    return init(context);
}

// ------------------------------------------------------------------------------------------------
// Calculates the node transformations for the scene.
void SceneAnimator::calculate(const U32 animationIndex, const D64 pTime)
{
    DIVIDE_ASSERT(_skeleton != nullptr);

    if (animationIndex >= _animations.size())
    {
        return;  // invalid animation
    }

    _animations[animationIndex]->evaluate(pTime, *_skeleton);
}

Bone* SceneAnimator::boneByNameHash(const U64 nameHash) const
{
    DIVIDE_ASSERT(_skeleton != nullptr);

    return _skeleton->find(nameHash);
}

U8 SceneAnimator::boneIndexByNameHash(const U64 nameHash) const
{
    const Bone* bone = boneByNameHash(nameHash);

    return bone != nullptr ? bone->_boneID : Bone::INVALID_BONE_IDX;
}

/// Renders the current skeleton pose at time index dt
const vector<Line>& SceneAnimator::skeletonLines(const U32 animationIndex, const D64 dt, bool forward)
{
    DIVIDE_ASSERT( animationIndex < _animations.size() );

    const I32 frameIndex = std::max(_animations[animationIndex]->frameIndexAt(dt, forward)._curr - 1, 0);

    I32& vecIndex = _skeletonLines[animationIndex][frameIndex];

    if (vecIndex == -1)
    {
        vecIndex = to_I32(_skeletonLinesContainer.size());
        _skeletonLinesContainer.emplace_back();
    }

    // create all the needed points
    vector<Line>& lines = _skeletonLinesContainer[vecIndex];
    if (lines.empty() && _skeleton != nullptr)
    {
        lines.reserve(boneCount());
        // Construct skeleton
        calculate(animationIndex, dt);
        // Start with identity transform
        CreateSkeleton(_skeleton, MAT4_IDENTITY, lines);
    }

    return lines;
}

void SceneAnimator::save([[maybe_unused]] PlatformContext& context, ByteBuffer& dataOut) const
{
    // first recursively save the skeleton
    if (_skeleton != nullptr)
    {
        saveSkeleton(dataOut, *_skeleton);
    }

    dataOut << BYTE_BUFFER_VERSION_ANIMATOR;

    // the number of animations
    const U32 nsize = to_U32(_animations.size());
    dataOut << nsize;

    for (U32 i = 0u; i < nsize; i++)
    {
        AnimEvaluator::save(*_animations[i], dataOut);
    }
}

void SceneAnimator::load(PlatformContext& context, ByteBuffer& dataIn)
{
    // make sure to clear this before writing new data
    release(true);
    assert(_animations.empty());

    _skeleton.reset(loadSkeleton(dataIn, nullptr));
    _skeletonDepthCache = to_U8(std::min(_skeleton->hierarchyDepth(), to_size(U8_MAX)));

    auto tempVer = decltype(BYTE_BUFFER_VERSION_ANIMATOR){0};
    dataIn >> tempVer;
    if (tempVer != BYTE_BUFFER_VERSION_ANIMATOR)
    {
        DIVIDE_UNEXPECTED_CALL();
    }

    // the number of animations
    U32 nsize = 0u;
    dataIn >> nsize;
    _animations.resize(nsize);

    for (U32 idx = 0u; idx < nsize; ++idx)
    {
        _animations[idx] = std::make_unique<AnimEvaluator>();
        AnimEvaluator::load(*_animations[idx], dataIn);
        // get all the animation names so I can reference them by name and get the correct id
        insert(_animationNameToID, _ID(_animations[idx]->name().c_str()), idx);
    }

    init(context);
}

void SceneAnimator::saveSkeleton(ByteBuffer& dataOut, const Bone& parent) const
{
    dataOut << BYTE_BUFFER_VERSION_SKELETON;

    // the name of the bone
    dataOut << parent.name();
    // the bone offsets
    dataOut << parent._offsetMatrix;
    // original bind pose
    dataOut << parent._localTransform;
    // bone ID
    dataOut << parent._boneID;

    // number of children
    const U32 nsize = to_U32(parent.children().size());
    dataOut << nsize;
    // continue for all children
    for (const Bone_uptr& child : parent.children())
    {
        saveSkeleton(dataOut, *child);
    }
}

Bone* SceneAnimator::loadSkeleton(ByteBuffer& dataIn, Bone* parentIn)
{
    auto tempVer = decltype(BYTE_BUFFER_VERSION_SKELETON){0};
    dataIn >> tempVer;
    if (tempVer != BYTE_BUFFER_VERSION_SKELETON)
    {
        DIVIDE_UNEXPECTED_CALL();
    }

    string tempString;
    // the name of the bone
    dataIn >> tempString;
    // create a node and set the parent, in the case this is the root node, it will be null
    Bone* internalNode = new Bone(tempString, parentIn);

    // the bone offsets
    dataIn >> internalNode->_offsetMatrix;

    // original bind pose
    dataIn >> internalNode->_localTransform;

    // bone ID
    dataIn >> internalNode->_boneID;

    // the number of children
    U32 nsize = 0u;
    dataIn >> nsize;

    // recursively call this function on all children
    // continue for all child nodes and assign the created internal nodes as our children
    for (U32 a = 0u; a < nsize; a++)
    {
        Bone* crtNode = loadSkeleton(dataIn, internalNode);
        DIVIDE_UNUSED(crtNode);
    }

    return internalNode;
}

} //namespace Divide
