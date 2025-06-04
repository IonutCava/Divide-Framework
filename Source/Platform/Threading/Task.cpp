

#include "Headers/Task.h"
#include "Utility/Headers/Localization.h"

namespace Divide
{

    void Start( Task& task, TaskPool& pool, const TaskPriority priority, DELEGATE<void>&& onCompletionFunction )
    {
        static_assert(sizeof(Task) == 128u);
        pool.enqueue( task, priority, MOV(onCompletionFunction) );
    }

    void Wait( const Task& task, TaskPool& pool )
    {
        pool.waitForTask( task );
    }

}; //namespace Divide
