

#include "Headers/ThreadPool.h"

namespace Divide
{
    ThreadPool::ThreadPool( TaskPool& parent, const U32 threadCount )
        : _parent( parent )
    {
        _threads.reserve( threadCount );

        for ( U32 idx = 0u; idx < threadCount; ++idx )
        {
            _threads.emplace_back(
                [&, idx]
                {
                    const std::thread::id threadID = std::this_thread::get_id();
                    _parent.onThreadCreate( idx, threadID );
                    while ( _isRunning )
                    {
                        executeOneTask( true );
                    }

                    _parent.onThreadDestroy( threadID );
                } );
        }
    }

    ThreadPool::~ThreadPool()
    {
        join();
    }

    void ThreadPool::join()
    {
        if ( !_isRunning )
        {
            return;
        }

        _isRunning = false;

        const size_t threadCount = _threads.size();
        for ( size_t idx = 0; idx < threadCount; ++idx )
        {
            addTask( []( [[maybe_unused]] const bool wait ) noexcept
                     {
                         return true;
                     } );
        }

        for ( std::thread& thread : _threads )
        {
            if ( thread.joinable() )
            {
                thread.join();
            }
        }
    }

    void ThreadPool::wait() const noexcept
    {
        if ( _isRunning )
        {
            // Busy wait
            while ( _tasksLeft.load() > 0 )
            {
                std::this_thread::yield();
            }
        }
    }

    bool ThreadPool::addTask( PoolTask&& job )
    {
        if ( _queue.enqueue( MOV( job ) ) )
        {
            _tasksLeft.fetch_add( 1 );
            return true;
        }

        return false;
    }

    void ThreadPool::executeOneTask( const bool waitForTask )
    {
        PoolTask task = {};
        if ( dequeTask( waitForTask, task ) )
        {
            if ( !task( !waitForTask ) )
            {
                addTask( MOV( task ) );
            }
            _tasksLeft.fetch_sub( 1 );
        }
    }

    bool ThreadPool::dequeTask( const bool waitForTask, PoolTask& taskOut )
    {
        if ( waitForTask )
        {
            if constexpr (IsBlocking )
            {
                _queue.wait_dequeue( taskOut );
            }
            else
            {
                while ( !_queue.try_dequeue( taskOut ) )
                {
                    std::this_thread::yield();
                }
            }
        }
        else if ( !_queue.try_dequeue( taskOut ) )
        {
            return false;
        }

        return true;
    }
} //namespace Divide
