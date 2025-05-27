

#include "Headers/Task.h"
#include "Utility/Headers/Localization.h"

namespace Divide
{

    void Start( Task& task, TaskPool& pool, const TaskPriority priority, DELEGATE<void>&& onCompletionFunction )
    {
        pool.enqueue( task, priority, MOV(onCompletionFunction) );
    }

    void Wait( const Task& task, TaskPool& pool )
    {
        pool.waitForTask( task );
    }

}; //namespace Divide
