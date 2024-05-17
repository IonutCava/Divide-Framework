

#include "Headers/Task.h"
#include "Utility/Headers/Localization.h"

namespace Divide
{

    void Start( Task& task, TaskPool& pool, const TaskPriority priority, const DELEGATE<void>& onCompletionFunction )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Threading );

        if ( !pool.enqueue( task, priority, onCompletionFunction ) ) [[unlikely]]
        {
            Console::errorfn( LOCALE_STR( "TASK_SCHEDULE_FAIL" ), 1 );
            Start( task, pool, TaskPriority::REALTIME, onCompletionFunction );
        }
    }

    void Wait( const Task& task, TaskPool& pool )
    {
        pool.waitForTask( task );
    }

}; //namespace Divide
