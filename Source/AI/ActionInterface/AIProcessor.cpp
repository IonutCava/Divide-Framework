

#include "Headers/AIProcessor.h"

#include "AI/Headers/AIManager.h"

namespace Divide::AI {

AIProcessor::AIProcessor(AIManager& parentManager)
    : _entity(nullptr),
      _parentManager(parentManager),
      _currentStep(U32_MAX),
      _activeGoal(nullptr)
{
    _init = false;
}

AIProcessor::~AIProcessor()
{
    _actionSet.clear();
    _goals.clear();
}

void AIProcessor::addEntityRef(AIEntity* entity) {
    if (entity) {
        _entity = entity;
    }
}

void AIProcessor::registerAction(const GOAPAction& action) {
    _actionSet.push_back(&action);
}

void AIProcessor::registerActionSet(const GOAPActionSet& actionSet) {
    _actionSet.insert(std::end(_actionSet), std::begin(actionSet),
                      std::end(actionSet));
}

void AIProcessor::registerGoal(const GOAPGoal& goal) {
    assert(!goal.vars_.empty());
    _goals.push_back(goal);
}

void AIProcessor::registerGoalList(const GOAPGoalList& goalList) {
    for (const GOAPGoal& goal : goalList) {
        registerGoal(goal);
    }
}

const char* AIProcessor::printActionStats( [[maybe_unused]] const GOAPAction& planStep ) const {
    constexpr const char* none = "";
    return none;
}

}  // namespace Divide::AI
