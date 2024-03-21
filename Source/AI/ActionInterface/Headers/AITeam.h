/*
   Copyright (c) 2018 DIVIDE-Studio
   Copyright (c) 2009 Ionut Cava

   This file is part of DIVIDE Framework.

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software
   and associated documentation files (the "Software"), to deal in the Software
   without restriction,
   including without limitation the rights to use, copy, modify, merge, publish,
   distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so,
   subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED,
   INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
   PARTICULAR PURPOSE AND NONINFRINGEMENT.
   IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
   DAMAGES OR OTHER LIABILITY,
   WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
   IN CONNECTION WITH THE SOFTWARE
   OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 */

#pragma once
#ifndef DVD_AI_TEAM_H_
#define DVD_AI_TEAM_H_

#include "AI/Headers/AIEntity.h"

namespace Divide {

class TaskPool;

namespace AI {

class AIManager;

namespace Navigation {
    class DivideDtCrowd;
    class NavigationMesh;
}

class Order {
   public:
    virtual ~Order() = default;
    Order(const U32 id) : _id(id), _locked(false) {}
    [[nodiscard]] U32 getID() const { return _id; }
    [[nodiscard]] bool locked() const { return _locked; }
    virtual void lock() { _locked = true; }
    virtual void unlock() { _locked = false; }

   protected:
    U32 _id;
    bool _locked;
};

class AITeam final : public GUIDWrapper {
   public:
    using CrowdPtr = Navigation::DivideDtCrowd*;
    using AITeamCrowd = hashMap<AIEntity::PresetAgentRadius, CrowdPtr>;
    using MemberVariable = hashMap<AIEntity*, F32>;
    using TeamMap = hashMap<I64, AIEntity*>;
    using OrderPtr = std::shared_ptr<Order>;
    using OrderList = vector<OrderPtr>;

   public:
    AITeam(U32 id, AIManager& parentManager);
    ~AITeam() override;

    CrowdPtr getCrowd(const AIEntity::PresetAgentRadius radius) const {
        SharedLock<SharedMutex> r_lock(_crowdMutex);
        const AITeamCrowd::const_iterator it = _aiTeamCrowd.find(radius);
        if (it != std::end(_aiTeamCrowd)) {
            return it->second;
        }
        return nullptr;
    }

    bool addTeamMember(AIEntity* entity);
    bool removeTeamMember(AIEntity* entity);
    bool addEnemyTeam(U32 enemyTeamID);
    bool removeEnemyTeam(U32 enemyTeamID);

    void setTeamID(const U32 value) { _teamID = value; }
    U32  getTeamID() const { return _teamID; }

    I32  getEnemyTeamID(const U32 index) const {
        if (_enemyTeams.size() <= index) {
            return -1;
        }
        return _enemyTeams[index];
    }

    const TeamMap& getTeamMembers() const { return _team; }
    MemberVariable& getMemberVariable() { return _memberVariable; }

    void clearOrders() {
        LockGuard<SharedMutex> w_lock(_orderMutex);
        _orders.clear();
    }

    void addOrder(const OrderPtr& order) {
        LockGuard<SharedMutex> w_lock(_orderMutex);
        if (findOrder(order->getID()) == std::end(_orders)) {
            _orders.push_back(order);
        }
    }

    void removeOrder(const Order& order) {
        LockGuard<SharedMutex> w_lock(_orderMutex);
        const OrderList::iterator it = findOrder(order);
        if (it != std::end(_orders)) {
            _orders.erase(it);
        }
    }

    const OrderList& requestOrders() const { return _orders; }

   protected:
    friend class AIManager;
    /// Update the crowding system
    void resetCrowd();
    [[nodiscard]] bool processInput(TaskPool& parentPool, U64 deltaTimeUS);
    [[nodiscard]] bool processData(TaskPool& parentPool, U64 deltaTimeUS);
    [[nodiscard]] bool update(TaskPool& parentPool, U64 deltaTimeUS);
    void addCrowd(AIEntity::PresetAgentRadius radius, Navigation::NavigationMesh* navMesh);
    void removeCrowd(AIEntity::PresetAgentRadius radius);

   protected:
    [[nodiscard]] vector<AIEntity*> getEntityList() const;

    [[nodiscard]] OrderList::iterator findOrder(const Order& order) {
        return findOrder(order.getID());
    }

    [[nodiscard]] OrderList::iterator findOrder(U32 orderID) {
        return eastl::find_if(
            eastl::begin(_orders), 
            eastl::end(_orders),
            [&orderID](const OrderPtr& order) -> bool {
                return orderID == order->getID();
            });
    }

    [[nodiscard]] vector<U32>::iterator findEnemyTeamEntry(U32 enemyTeamID) {
        return eastl::find_if(
            begin(_enemyTeams), end(_enemyTeams),
            [&enemyTeamID](const U32 id) -> bool { return id == enemyTeamID; });
    }

   protected:
    mutable SharedMutex _orderMutex;

   private:
    U32 _teamID;
    TeamMap _team;
    AIManager& _parentManager;
    /// Container with data per team member. For example a map of distances
    MemberVariable _memberVariable;
    AITeamCrowd _aiTeamCrowd;
    mutable SharedMutex _updateMutex;
    mutable SharedMutex _crowdMutex;
    vector<U32> _enemyTeams;
    OrderList _orders;
};

}  // namespace AI
}  // namespace Divide

#endif //DVD_AI_TEAM_H_
