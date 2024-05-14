

#include "Headers/glHardwareQuery.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/GLWrapper.h"

namespace Divide {

glHardwareQuery::glHardwareQuery() noexcept
    : _enabled(false)
{
}

void glHardwareQuery::create([[maybe_unused]] const gl46core::GLenum queryType)
{
    destroy();
    gl46core::glGenQueries(1, &_queryID);
}

void glHardwareQuery::destroy()
{
    if (_queryID != 0u)
    {
        gl46core::glDeleteQueries(1, &_queryID);
    }
    _queryID = 0u;
}

bool glHardwareQuery::isResultAvailable() const
{
    gl46core::GLint available = 0;
    gl46core::glGetQueryObjectiv(getID(), gl46core::GL_QUERY_RESULT_AVAILABLE, &available);
    return available != 0;
}

I64 glHardwareQuery::getResult() const
{
    gl46core::GLint64 res = 0;
    gl46core::glGetQueryObjecti64v(getID(), gl46core::GL_QUERY_RESULT, &res);
    return res;
}

I64 glHardwareQuery::getResultNoWait() const
{
    gl46core::GLint64 res = 0;
    gl46core::glGetQueryObjecti64v(getID(), gl46core::GL_QUERY_RESULT_NO_WAIT, &res);
    return res;
}

glHardwareQueryRing::glHardwareQueryRing(GFXDevice& context, const gl46core::GLenum queryType, const U16 queueLength, const U32 id)
    : RingBufferSeparateWrite(queueLength, true),
      _context(context),
      _id(id),
      _queryType(queryType)
{
    _queries.reserve(queueLength);
    resize(queueLength);
}

glHardwareQueryRing::~glHardwareQueryRing()
{
    for (glHardwareQuery& query : _queries) {
        query.destroy();
    }
    _queries.clear();
}

const glHardwareQuery& glHardwareQueryRing::readQuery() const
{
    return _queries[queueReadIndex()];
}

const glHardwareQuery& glHardwareQueryRing::writeQuery() const
{
    return _queries[queueWriteIndex()];
}

void glHardwareQueryRing::resize(const U16 queueLength)
{
    RingBufferSeparateWrite::resize(queueLength);

    const size_t crtCount = _queries.size();
    if (queueLength < crtCount)
    {
        while (queueLength < crtCount)
        {
            _queries.back().destroy();
            _queries.pop_back();
        }
    }
    else
    {
        const size_t countToAdd = queueLength - crtCount;

        for (size_t i = 0; i < countToAdd; ++i)
        {
            _queries.emplace_back().create(_queryType);

            //Prime the query
            gl46core::glBeginQuery(_queryType, _queries.back().getID());
            gl46core::glEndQuery(_queryType);
        }
    }
}


void glHardwareQueryRing::begin() const
{
    gl46core::glBeginQuery(_queryType, writeQuery().getID());
}

void glHardwareQueryRing::end() const
{
    gl46core::glEndQuery(_queryType);
}

glHardwareQueryPool::glHardwareQueryPool(GFXDevice& context)
    : _context(context)
{
}

glHardwareQueryPool::~glHardwareQueryPool()
{
    destroy();
}

void glHardwareQueryPool::init(const hashMap<gl46core::GLenum, U32>& sizes)
{
    destroy();
    for (auto [type, size] : sizes) 
    {
        const U32 j = std::max(size, 1u);
        _index[type] = 0;
        auto& pool = _queryPool[type];
        for (U32 i = 0; i < j; ++i)
        {
            pool.emplace_back(std::make_unique<glHardwareQueryRing>(_context, type, 1, i));
        }
    }
}

void glHardwareQueryPool::destroy()
{
    _queryPool.clear();
}

glHardwareQueryRing& glHardwareQueryPool::allocate(const gl46core::GLenum queryType)
{
    return *_queryPool[queryType][++_index[queryType]];
}

void glHardwareQueryPool::deallocate(const glHardwareQueryRing& query)
{
    const gl46core::GLenum type = query.type();
    U32& index = _index[type];
    auto& pool = _queryPool[type];
    for (U32 i = 0; i < index; ++i)
    {
        if (pool[i]->id() == query.id())
        {
            std::swap(pool[i], pool[index - 1]);
            --index;
            return;
        }
    }

    DIVIDE_UNEXPECTED_CALL();
}

} //namespace Divide
