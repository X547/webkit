/*
 * Copyright (C) 2019 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"

#if ENABLE(ACCESSIBILITY_ISOLATED_TREE)
#include "AXIsolatedTree.h"

#include "AXIsolatedObject.h"
#include "Page.h"
#include <wtf/NeverDestroyed.h>

namespace WebCore {

static Lock s_cacheLock;

static unsigned newTreeID()
{
    static unsigned s_currentTreeID = 0;
    return ++s_currentTreeID;
}

AXIsolatedTree::NodeChange::NodeChange(AXIsolatedObject& isolatedObject, AccessibilityObjectWrapper* wrapper)
    : m_isolatedObject(isolatedObject)
    , m_wrapper(wrapper)
{
}

AXIsolatedTree::NodeChange::NodeChange(const NodeChange& other)
    : m_isolatedObject(other.m_isolatedObject.get())
    , m_wrapper(other.m_wrapper)
{
}

HashMap<PageIdentifier, Ref<AXIsolatedTree>>& AXIsolatedTree::treePageCache()
{
    static NeverDestroyed<HashMap<PageIdentifier, Ref<AXIsolatedTree>>> map;
    return map;
}

HashMap<AXIsolatedTreeID, Ref<AXIsolatedTree>>& AXIsolatedTree::treeIDCache()
{
    static NeverDestroyed<HashMap<AXIsolatedTreeID, Ref<AXIsolatedTree>>> map;
    return map;
}

AXIsolatedTree::AXIsolatedTree()
    : m_treeID(newTreeID())
{
}

AXIsolatedTree::~AXIsolatedTree() = default;

Ref<AXIsolatedTree> AXIsolatedTree::create()
{
    ASSERT(isMainThread());
    return adoptRef(*new AXIsolatedTree());
}

RefPtr<AXIsolatedObject> AXIsolatedTree::nodeInTreeForID(AXIsolatedTreeID treeID, AXID axID)
{
    return treeForID(treeID)->nodeForID(axID);
}

RefPtr<AXIsolatedTree> AXIsolatedTree::treeForID(AXIsolatedTreeID treeID)
{
    return treeIDCache().get(treeID);
}

Ref<AXIsolatedTree> AXIsolatedTree::createTreeForPageID(PageIdentifier pageID)
{
    LockHolder locker(s_cacheLock);
    ASSERT(!treePageCache().contains(pageID));

    auto newTree = AXIsolatedTree::create();
    treePageCache().set(pageID, newTree.copyRef());
    treeIDCache().set(newTree->treeIdentifier(), newTree.copyRef());
    return newTree;
}

void AXIsolatedTree::removeTreeForPageID(PageIdentifier pageID)
{
    ASSERT(isMainThread());
    LockHolder locker(s_cacheLock);

    if (auto optionalTree = treePageCache().take(pageID)) {
        auto& tree { *optionalTree };

        LockHolder treeLocker { tree->m_changeLogLock };
        tree->m_pendingSubtreeRemovals.append(tree->m_rootNodeID);
        tree->setAXObjectCache(nullptr);
        treeLocker.unlockEarly();

        treeIDCache().remove(tree->treeIdentifier());
    }
}

RefPtr<AXIsolatedTree> AXIsolatedTree::treeForPageID(PageIdentifier pageID)
{
    LockHolder locker(s_cacheLock);

    if (auto tree = treePageCache().get(pageID))
        return makeRefPtr(tree);

    return nullptr;
}

RefPtr<AXIsolatedObject> AXIsolatedTree::nodeForID(AXID axID) const
{
    return axID != InvalidAXID ? m_readerThreadNodeMap.get(axID) : nullptr;
}

Vector<RefPtr<AXCoreObject>> AXIsolatedTree::objectsForIDs(Vector<AXID> axIDs) const
{
    Vector<RefPtr<AXCoreObject>> result;
    result.reserveCapacity(axIDs.size());

    for (const auto& axID : axIDs) {
        if (auto object = nodeForID(axID))
            result.uncheckedAppend(object);
    }

    return result;
}

void AXIsolatedTree::generateSubtree(AXCoreObject& axObject, AXID parentID, bool attachWrapper)
{
    ASSERT(isMainThread());
    Vector<NodeChange> nodeChanges;
    auto object = createSubtree(axObject, parentID, attachWrapper, nodeChanges);
    appendNodeChanges(nodeChanges);

    if (parentID == InvalidAXID)
        setRootNode(object);
    // FIXME: else attach the newly created subtree to its parent.
}

Ref<AXIsolatedObject> AXIsolatedTree::createSubtree(AXCoreObject& axObject, AXID parentID, bool attachWrapper, Vector<NodeChange>& nodeChanges)
{
    ASSERT(isMainThread());
    auto object = AXIsolatedObject::create(axObject, m_treeID, parentID);
    if (attachWrapper) {
        object->attachPlatformWrapper(axObject.wrapper());
        // Since this object has already an attached wrapper, set the wrapper
        // in the NodeChange to null so that it is not re-attached.
        nodeChanges.append(NodeChange(object, nullptr));
    } else {
        // Set the wrapper in the NodeChange so that it is set on the AX thread.
        nodeChanges.append(NodeChange(object, axObject.wrapper()));
    }

    for (const auto& axChild : axObject.children()) {
        auto child = createSubtree(*axChild, object->objectID(), attachWrapper, nodeChanges);
        object->appendChild(child->objectID());
    }

    return object;
}

void AXIsolatedTree::updateNode(AXCoreObject& axObject)
{
    ASSERT(isMainThread());
    AXID axID = axObject.objectID();
    auto* axParent = axObject.parentObject();
    AXID parentID = axParent ? axParent->objectID() : InvalidAXID;

    if (auto object = nodeForID(axID)) {
        ASSERT(object->objectID() == axID);
        auto newObject = AXIsolatedObject::create(axObject, m_treeID, parentID);

        LockHolder locker { m_changeLogLock };
        // The new object should have the same children as the old one.
        newObject->m_childrenIDs = object->m_childrenIDs;
        // Remove the old object and set the new one to be updated on the AX thread.
        m_pendingNodeRemovals.append(axID);
        m_pendingAppends.append(NodeChange(newObject, axObject.wrapper()));
    }
}

void AXIsolatedTree::updateSubtree(AXCoreObject& axObject)
{
    ASSERT(isMainThread());
    removeSubtree(axObject.objectID());
    auto* axParent = axObject.parentObject();
    AXID parentID = axParent ? axParent->objectID() : InvalidAXID;
    generateSubtree(axObject, parentID, false);
}

void AXIsolatedTree::updateChildren(AXCoreObject& axObject)
{
    ASSERT(isMainThread());
    AXID axObjectID = axObject.objectID();
    auto object = nodeForID(axObjectID);
    if (!object)
        return; // nothing to update.

    const auto& axChildren = axObject.children();
    auto axChildrenIDs = axObject.childrenIDs();

    LockHolder locker { m_changeLogLock };
    auto removals = object->m_childrenIDs;
    // Make the children IDs of the isolated object to be the same as the AXObject's.
    object->m_childrenIDs = axChildrenIDs;
    locker.unlockEarly();

    for (size_t i = 0; i < axChildrenIDs.size(); ++i) {
        size_t index = removals.find(axChildrenIDs[i]);
        if (index != notFound)
            removals.remove(index);
        else {
            // This is a new child, add it to the tree.
            generateSubtree(*axChildren[i], axObjectID, false);
        }
    }

    // What is left in removals are the IDs that are no longer children of
    // axObject. Thus, remove them from the tree.
    for (const AXID& childID : removals)
        removeSubtree(childID);
}

RefPtr<AXIsolatedObject> AXIsolatedTree::focusedUIElement()
{
    return nodeForID(m_focusedNodeID);
}
    
RefPtr<AXIsolatedObject> AXIsolatedTree::rootNode()
{
    return nodeForID(m_rootNodeID);
}

void AXIsolatedTree::setRootNode(Ref<AXIsolatedObject>& root)
{
    LockHolder locker { m_changeLogLock };
    m_rootNodeID = root->objectID();
    m_readerThreadNodeMap.add(root->objectID(), WTFMove(root));
}
    
void AXIsolatedTree::setFocusedNode(AXID axID)
{
    ASSERT(isMainThread());
    LockHolder locker { m_changeLogLock };
    m_focusedNodeID = axID;
    if (axID == InvalidAXID)
        return;

    if (m_readerThreadNodeMap.contains(m_focusedNodeID))
        return; // Nothing to do, the focus is set.

    // If the focused object is in the pending appends, add it to the reader
    // map, so that we can return the right focused object if requested before
    // pending appends are applied.
    for (const auto& item : m_pendingAppends) {
        if (item.m_isolatedObject->objectID() == m_focusedNodeID
            && m_readerThreadNodeMap.add(m_focusedNodeID, item.m_isolatedObject.get()) && item.m_wrapper)
            m_readerThreadNodeMap.get(m_focusedNodeID)->attachPlatformWrapper(item.m_wrapper.get());
    }
}

void AXIsolatedTree::setFocusedNodeID(AXID axID)
{
    LockHolder locker { m_changeLogLock };
    m_pendingFocusedNodeID = axID;
}

void AXIsolatedTree::removeNode(AXID axID)
{
    LockHolder locker { m_changeLogLock };
    m_pendingNodeRemovals.append(axID);
}

void AXIsolatedTree::removeSubtree(AXID axID)
{
    LockHolder locker { m_changeLogLock };
    m_pendingSubtreeRemovals.append(axID);
}

void AXIsolatedTree::appendNodeChanges(const Vector<NodeChange>& changes)
{
    ASSERT(isMainThread());
    m_pendingAppends.appendVector(changes);
}

void AXIsolatedTree::applyPendingChanges()
{
    RELEASE_ASSERT(!isMainThread());
    LockHolder locker { m_changeLogLock };

    m_focusedNodeID = m_pendingFocusedNodeID;

    while (m_pendingNodeRemovals.size()) {
        auto axID = m_pendingNodeRemovals.takeLast();
        if (axID == InvalidAXID)
            continue;

        if (auto object = nodeForID(axID))
            object->detach(AccessibilityDetachmentType::ElementDestroyed);
    }

    while (m_pendingSubtreeRemovals.size()) {
        auto axID = m_pendingSubtreeRemovals.takeLast();
        if (axID == InvalidAXID)
            continue;

        if (auto object = nodeForID(axID)) {
            object->detach(AccessibilityDetachmentType::ElementDestroyed);
            m_pendingSubtreeRemovals.appendVector(object->m_childrenIDs);
        }
    }

    for (const auto& item : m_pendingAppends) {
        ASSERT(item.m_isolatedObject->wrapper() || item.m_wrapper);
        AXID axID = item.m_isolatedObject->objectID();
        if (axID == InvalidAXID)
            continue;

        if (auto object = m_readerThreadNodeMap.get(axID)) {
            if (object != &item.m_isolatedObject.get()
                && (object->wrapper() == item.m_wrapper || object->wrapper() == item.m_isolatedObject->wrapper())) {
                // The new IsolatedObject is a replacement for an existing object
                // as the result of an update. Thus detach the existing one before
                // adding the new one.
                object->detachWrapper(AccessibilityDetachmentType::ElementDestroyed);
            }
            m_readerThreadNodeMap.remove(axID);
        }

        if (m_readerThreadNodeMap.add(axID, item.m_isolatedObject.get()) && item.m_wrapper)
            m_readerThreadNodeMap.get(axID)->attachPlatformWrapper(item.m_wrapper.get());

        // The reference count of the just added IsolatedObject must be 2
        // because it is referenced by m_readerThreadNodeMap and m_pendingAppends.
        // When m_pendingAppends is cleared, the object will be held only by m_readerThreadNodeMap.
        ASSERT(m_readerThreadNodeMap.get(axID)->refCount() == 2);
    }
    m_pendingAppends.clear();
}

} // namespace WebCore

#endif // ENABLE(ACCESSIBILITY_ISOLATED_TREE)
