/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <Atom/RHI/DeviceResource.h>
#include <Atom/RHI/MultiDeviceObject.h>
#include <Atom/RHI/ResourceViewCache.h>


namespace AZ::RHI
{
    class ResourcePool;
    class FrameAttachment;
    class MemoryStatisticsBuilder;
    class ResourceView;
    class ImageView;
    class BufferView;
    struct ImageViewDescriptor;
    struct BufferViewDescriptor;

    //! Resource is a base class for pooled, multi-device RHI resources (Image / Buffer /
    //! ShaderResourceGroup, etc). It provides some common lifecycle management semantics. Resource creation is
    //! separate from initialization. Resources are created separate from any pool, but its backing platform data is associated at
    //! initialization time on a specific pool.
    class Resource : public MultiDeviceObject
    {
        friend class FrameAttachment;
        friend class ResourcePool;

    public:
        AZ_RTTI(Resource, "{613AED98-48FD-4453-98F8-6956D2133489}", MultiDeviceObject);
        virtual ~Resource();
        AZ_RHI_MULTI_DEVICE_OBJECT_GETTER(Resource);

        //! Returns whether the resource is currently an attachment on a frame graph.
        bool IsAttachment() const;

        //! Shuts down the resource by detaching it from its parent pool.
        virtual void Shutdown() override = 0;

        //! Returns the parent pool this resource is registered on. Since resource creation is
        //! separate from initialization, this will be null until the resource is registered on a pool.
        const ResourcePool* GetPool() const;
        ResourcePool* GetPool();

        //! Returns the version number. This number is monotonically increased anytime
        //! new platform memory is assigned to the resource. Any dependent resource is
        //! valid so long as the version numbers match.
        uint32_t GetVersion() const;

        //! Returns the frame attachment associated with this image (if it exists).
        const FrameAttachment* GetFrameAttachment() const;

        //! Invalidates all views referencing this resource. Invalidation is handled implicitly
        //! on a Shutdown / Init cycle from the pool. For example, it is safe to create a resource,
        //! create a view to that resource, and then Shutdown / Re-Init the resource. InvalidateViews is
        //! called to synchronize views (and shader resource groups which hold them) to the new data.
        //!
        //! Platform back-ends which invalidate GPU-specific data on the resource without an explicit
        //! shutdown / re-initialization will need to call this method explicitly.
        void InvalidateViews();

        //! Returns true if the ResourceView is in the cache
        bool IsInResourceCache(const ImageViewDescriptor& imageViewDescriptor);
        bool IsInResourceCache(const BufferViewDescriptor& bufferViewDescriptor);

        void EraseResourceView(ResourceView* resourceView) const;

    protected:
        Resource() = default;

        //! Returns view based on the descriptor and keeps a raw pointer in a local cache
        Ptr<ImageView> GetResourceView(const ImageViewDescriptor& imageViewDescriptor) const;
        Ptr<BufferView> GetResourceView(const BufferViewDescriptor& bufferViewDescriptor) const;

    private:
        //! Returns whether this resource has been initialized before.
        bool IsFirstVersion() const;

        //! Called by the parent pool at initialization time.
        void SetPool(ResourcePool* pool);

        //! Called by the frame attachment at frame building time.
        void SetFrameAttachment(FrameAttachment* frameAttachment, int deviceIndex = MultiDevice::InvalidDeviceIndex);

        //! The parent pool this resource is registered with.
        ResourcePool* m_pool = nullptr;

        //! The current frame attachment registered on this resource.
        FrameAttachment* m_frameAttachment = nullptr;

        //! The version is monotonically incremented any time the backing resource is changed.
        uint32_t m_version = 0;

        //! Cache the resourceViews in order to avoid re-creation
        //! Since ResourceView has a dependency to Resource this cache holds raw pointers here in order to ensure there
        //! is no circular dependency between the resource and it's resourceview.
        mutable ResourceViewCache<Resource> m_resourceViewCache;
    };

    class Buffer;
    class Image;
    class DeviceResourceView;

} // namespace AZ::RHI
