/****************************************************************************
Copyright (c) 2013-2016 Chukong Technologies Inc.
Copyright (c) 2017-2018 Xiamen Yaji Software Co., Ltd.

https://axmol.dev/

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
****************************************************************************/

#include "2d/ComponentContainer.h"
#include "2d/Component.h"
#include "2d/Node.h"

#include <algorithm>
#include <vector>

namespace ax
{

namespace
{
// SuperEditor: callback komponente (npr. Lua onEnter/update koji pozove Anim.play)
// sme da doda ili ukloni komponentu ISTOG noda. Iteracija direktno po robin_map-u
// bi tada citala invalidiran iterator (EXC_BAD_ACCESS u onEnter) — zato se prvo
// napravi kopija (retain), pa se zove samo ono sto je i dalje zakaceno za owner-a.
class ComponentSnapshot
{
public:
    explicit ComponentSnapshot(const hlookup::string_map<Component*>& map) : _size(map.size())
    {
        if (_size > kInline)
        {
            _heap.resize(_size);
            _data = _heap.data();
        }
        size_t i = 0;
        for (auto&& iter : map)
        {
            iter.second->retain();
            _data[i++] = iter.second;
        }
    }
    ~ComponentSnapshot()
    {
        for (size_t i = 0; i < _size; ++i)
            _data[i]->release();
    }
    ComponentSnapshot(const ComponentSnapshot&)            = delete;
    ComponentSnapshot& operator=(const ComponentSnapshot&) = delete;

    Component* const* begin() const { return _data; }
    Component* const* end() const { return _data + _size; }

private:
    static constexpr size_t kInline = 8;  // vecina nodova ima 1-3 komponente: bez alokacije po frejmu
    Component* _local[kInline];
    std::vector<Component*> _heap;
    Component** _data = _local;
    size_t _size;
};
}  // namespace

ComponentContainer::ComponentContainer(Node* node) : _owner(node) {}

ComponentContainer::~ComponentContainer() {}

Component* ComponentContainer::get(std::string_view name) const
{
    Component* ret = nullptr;

    auto it = _componentMap.find(name);
    if (it != _componentMap.end())
    {
        ret = it->second;
    }

    return ret;
}

bool ComponentContainer::add(Component* com)
{
    bool ret = false;
    AXASSERT(com != nullptr, "Component must be non-nil");
    AXASSERT(com->getOwner() == nullptr, "Component already added. It can't be added again");
    do
    {
        auto componentName = com->getName();

        if (_componentMap.find(componentName) != _componentMap.end())
        {
            AXASSERT(false, "ComponentContainer already have this kind of component");
            break;
        }
        hlookup::set_item(_componentMap, componentName, com);  //_componentMap[componentName] = com;
        com->retain();
        com->setOwner(_owner);
        com->onAdd();

        ret = true;
    } while (0);
    return ret;
}

bool ComponentContainer::remove(std::string_view componentName)
{
    bool ret = false;
    do
    {
        auto iter = _componentMap.find(componentName);
        AX_BREAK_IF(iter == _componentMap.end());

        auto component = iter->second;
        _componentMap.erase(componentName);

        component->onRemove();
        component->setOwner(nullptr);
        component->release();

        ret = true;
    } while (0);

    return ret;
}

bool ComponentContainer::remove(Component* com)
{
    return remove(com->getName());
}

void ComponentContainer::removeAll()
{
    if (!_componentMap.empty())
    {
        // SuperEditor: mapa se isprazni PRE callback-a (onRemove sme da dira kontejner)
        auto removed = std::move(_componentMap);
        _componentMap.clear();
        for (auto&& iter : removed)
        {
            iter.second->onRemove();
            iter.second->setOwner(nullptr);
            iter.second->release();
        }

        _owner->unscheduleUpdate();
    }
}

void ComponentContainer::visit(float delta)
{
    if (!_componentMap.empty())
    {
        AX_SAFE_RETAIN(_owner);
        ComponentSnapshot snapshot(_componentMap);  // dodate tokom update-a krecu od sledeceg frejma
        for (auto* com : snapshot)
        {
            if (com->getOwner() == _owner)
                com->update(delta);
        }
        AX_SAFE_RELEASE(_owner);
    }
}

void ComponentContainer::onEnter()
{
    // Node::onEnter postavlja _running tek posle ovoga, pa Node::addComponent komponenti
    // dodatoj tokom petlje NE zove onEnter -> dobija ga ovde (u novom krugu, do stabilnog stanja).
    std::vector<Component*> entered;
    while (true)
    {
        ComponentSnapshot snapshot(_componentMap);
        bool any = false;
        for (auto* com : snapshot)
        {
            if (com->getOwner() != _owner || std::find(entered.begin(), entered.end(), com) != entered.end())
                continue;
            entered.push_back(com);
            any = true;
            com->onEnter();
        }
        if (!any)
            break;
    }
}

void ComponentContainer::onExit()
{
    ComponentSnapshot snapshot(_componentMap);
    for (auto* com : snapshot)
    {
        if (com->getOwner() == _owner)
            com->onExit();
    }
}

}
