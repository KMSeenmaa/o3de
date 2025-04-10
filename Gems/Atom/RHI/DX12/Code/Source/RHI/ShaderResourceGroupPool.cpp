/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AtomCore/std/containers/small_vector.h>
#include <RHI/Buffer.h>
#include <RHI/BufferView.h>
#include <RHI/Conversions.h>
#include <RHI/DescriptorContext.h>
#include <RHI/Device.h>
#include <RHI/Image.h>
#include <RHI/ImageView.h>
#include <RHI/ShaderResourceGroupPool.h>

namespace AZ
{
    namespace DX12
    {
        template<typename T, typename U>
        void ShaderResourceGroupPool::GetSRVsFromImageViews(
            const AZStd::span<const RHI::ConstPtr<T>>& imageViews,
            D3D12_SRV_DIMENSION dimension,
            AZStd::small_vector<DescriptorHandle, SRGViewsFixedSize>& result)
        {
            result.resize(imageViews.size(), m_descriptorContext->GetNullHandleSRV(dimension));

            for (size_t i = 0; i < result.size(); ++i)
            {
                if (imageViews[i])
                {
                    result.span()[i] = AZStd::static_pointer_cast<const U>(imageViews[i])->GetReadDescriptor();
                }
            }
        }

        template<typename T, typename U>
        void ShaderResourceGroupPool::GetUAVsFromImageViews(
            const AZStd::span<const RHI::ConstPtr<T>>& imageViews,
            D3D12_UAV_DIMENSION dimension,
            AZStd::small_vector<DescriptorHandle, SRGViewsFixedSize>& result)
        {
            result.resize(imageViews.size(), m_descriptorContext->GetNullHandleUAV(dimension));
            for (size_t i = 0; i < result.size(); ++i)
            {
                if (imageViews[i])
                {
                    result.span()[i] = AZStd::static_pointer_cast<const U>(imageViews[i])->GetReadWriteDescriptor();
                }
            }
        }

        void ShaderResourceGroupPool::GetCBVsFromBufferViews(
            const AZStd::span<const RHI::ConstPtr<RHI::DeviceBufferView>>& bufferViews,
            AZStd::small_vector<DescriptorHandle, SRGViewsFixedSize>& result)
        {
            result.resize(bufferViews.size(), m_descriptorContext->GetNullHandleCBV());

            for (size_t i = 0; i < bufferViews.size(); ++i)
            {
                if (bufferViews[i])
                {
                    result.span()[i] = AZStd::static_pointer_cast<const BufferView>(bufferViews[i])->GetConstantDescriptor();
                }
            }
        }

        RHI::Ptr<ShaderResourceGroupPool> ShaderResourceGroupPool::Create()
        {
            return aznew ShaderResourceGroupPool();
        }

        RHI::ResultCode ShaderResourceGroupPool::InitInternal(
            RHI::Device& deviceBase,
            const RHI::ShaderResourceGroupPoolDescriptor& descriptor)
        {
            Device& device = static_cast<Device&>(deviceBase);
            m_descriptorContext = &device.GetDescriptorContext();

            const RHI::ShaderResourceGroupLayout& layout = *descriptor.m_layout;
            m_constantBufferSize = RHI::AlignUp(layout.GetConstantDataSize(), Alignment::Constant);
            m_constantBufferRingSize = m_constantBufferSize * RHI::Limits::Device::FrameCountMax;
            m_viewsDescriptorTableSize = layout.GetGroupSizeForBuffers() + layout.GetGroupSizeForImages();
            m_viewsDescriptorTableRingSize = m_viewsDescriptorTableSize * RHI::Limits::Device::FrameCountMax;
            m_samplersDescriptorTableSize = layout.GetGroupSizeForSamplers();
            m_samplersDescriptorTableRingSize = m_samplersDescriptorTableSize * RHI::Limits::Device::FrameCountMax;

            // Buffers occupy the first region of the table, then images.
            m_descriptorTableBufferOffset = 0;
            m_descriptorTableImageOffset = layout.GetGroupSizeForBuffers();

            m_unboundedArrayCount = layout.GetGroupSizeForBufferUnboundedArrays() + layout.GetGroupSizeForImageUnboundedArrays();

            if (m_constantBufferSize)
            {
                MemoryPoolSubAllocator::Descriptor allocatorDescriptor;
                allocatorDescriptor.m_garbageCollectLatency = RHI::Limits::Device::FrameCountMax;
                allocatorDescriptor.m_elementSize = m_constantBufferRingSize;
                m_constantAllocator.Init(allocatorDescriptor, device.GetConstantMemoryPageAllocator());
            }

            return RHI::ResultCode::Success;
        }

        void ShaderResourceGroupPool::ShutdownInternal()
        {
            if (m_constantBufferSize)
            {
                m_constantAllocator.Shutdown();
            }
            Base::ShutdownInternal();
        }

        RHI::ResultCode ShaderResourceGroupPool::InitGroupInternal(RHI::DeviceShaderResourceGroup& groupBase)
        {
            ShaderResourceGroup& group = static_cast<ShaderResourceGroup&>(groupBase);

            const uint32_t copyCount = RHI::Limits::Device::FrameCountMax;

            if (m_constantBufferSize)
            {
                group.m_constantMemoryView = MemoryView(m_constantAllocator.Allocate(m_constantBufferRingSize, RHI::Alignment::Constant), MemoryViewType::Buffer);
                CpuVirtualAddress cpuAddress = group.m_constantMemoryView.Map(RHI::HostMemoryAccess::Write);
                GpuVirtualAddress gpuAddress = group.m_constantMemoryView.GetGpuAddress();

                for (uint32_t i = 0; i < copyCount; ++i)
                {
                    ShaderResourceGroupCompiledData& compiledData = group.m_compiledData[i];
                    compiledData.m_gpuConstantAddress = gpuAddress + m_constantBufferSize * i;
                    compiledData.m_cpuConstantAddress = cpuAddress + m_constantBufferSize * i;
                }
            }
   
            if (m_samplersDescriptorTableSize)
            {
                group.m_samplersDescriptorTable = m_descriptorContext->CreateDescriptorTable(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, m_samplersDescriptorTableRingSize);

                if (!group.m_samplersDescriptorTable.IsValid())
                {
                    AZ_Error(
                        "ShaderResourceGroupPool", false,
                        "Descriptor context failed to allocate sampler descriptor table. Please consider increasing number of handles "
                        "allowed for the second value of DESCRIPTOR_HEAP_TYPE_SAMPLER within platformlimits.azasset file for dx12.");
                    return RHI::ResultCode::OutOfMemory;
                }

                for (uint32_t i = 0; i < copyCount; ++i)
                {
                    const DescriptorHandle descriptorHandle = group.m_samplersDescriptorTable.GetOffset() + m_samplersDescriptorTableSize * i;

                    ShaderResourceGroupCompiledData& compiledData = group.m_compiledData[i];
                    compiledData.m_gpuSamplersDescriptorHandle = m_descriptorContext->GetGpuPlatformHandleForTable(DescriptorTable(descriptorHandle, static_cast<uint16_t>(m_samplersDescriptorTableSize)));
                }
            }

            return RHI::ResultCode::Success;
        }

        void ShaderResourceGroupPool::ShutdownResourceInternal(RHI::DeviceResource& resourceBase)
        {
            ShaderResourceGroup& group = static_cast<ShaderResourceGroup&>(resourceBase);

            if (m_constantBufferSize)
            {
                group.m_constantMemoryView.Unmap(RHI::HostMemoryAccess::Write);
                m_constantAllocator.DeAllocate(group.m_constantMemoryView.m_memoryAllocation);
            }

            if (m_viewsDescriptorTableSize)
            {
                if (group.m_viewsDescriptorTable.IsValid())
                {
                    m_descriptorContext->ReleaseDescriptorTable(group.m_viewsDescriptorTable);
                }
            }

            if (m_samplersDescriptorTableSize)
            {
                if (group.m_viewsDescriptorTable.IsValid())
                {
                    m_descriptorContext->ReleaseDescriptorTable(group.m_samplersDescriptorTable);
                }
            }

            for (uint32_t unboundedArrayindex = 0; unboundedArrayindex < (ShaderResourceGroupCompiledData::MaxUnboundedArrays * RHI::Limits::Device::FrameCountMax); ++unboundedArrayindex)
            {
                if (group.m_unboundedDescriptorTables[unboundedArrayindex].IsValid())
                {
                    m_descriptorContext->ReleaseDescriptorTable(group.m_unboundedDescriptorTables[unboundedArrayindex]);
                }
            }

            group.m_compiledDataIndex = 0;
            group.m_compiledData.fill(ShaderResourceGroupCompiledData());

            Base::ShutdownResourceInternal(resourceBase);
        }

        RHI::ResultCode ShaderResourceGroupPool::CompileGroupInternal(
            RHI::DeviceShaderResourceGroup& groupBase,
            const RHI::DeviceShaderResourceGroupData& groupData)
        {
            typedef AZ::RHI::DeviceShaderResourceGroupData::ResourceTypeMask ResourceMask;
            ShaderResourceGroup& group = static_cast<ShaderResourceGroup&>(groupBase);

            group.m_compiledDataIndex = (group.m_compiledDataIndex + 1) % RHI::Limits::Device::FrameCountMax;
            
            if (m_constantBufferSize && groupBase.IsResourceTypeEnabledForCompilation(static_cast<uint32_t>(ResourceMask::ConstantDataMask)))
            {
                memcpy(group.GetCompiledData().m_cpuConstantAddress, groupData.GetConstantData().data(), groupData.GetConstantData().size());
            }

            if (m_viewsDescriptorTableSize)
            {
                //Lazy initialization for cbv/srv/uav Descriptor Tables
                if (!group.m_viewsDescriptorTable.IsValid())
                {
                    group.m_viewsDescriptorTable = m_descriptorContext->CreateDescriptorTable(
                        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, m_viewsDescriptorTableRingSize);

                    if (!group.m_viewsDescriptorTable.IsValid())
                    {
                        AZ_Assert(
                            false,
                            "Descriptor heap ran out of memory. Please consider increasing number of handles allowed for the second value"
                            "of DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV within platformlimits.azasset file for dx12.");
                        return RHI::ResultCode::OutOfMemory;
                    }

                    CacheGpuHandlesForViews(group);
                }

                const DescriptorTable descriptorTable(
                    group.m_viewsDescriptorTable.GetOffset() + group.m_compiledDataIndex * m_viewsDescriptorTableSize,
                    static_cast<uint16_t>(m_viewsDescriptorTableSize));

                UpdateViewsDescriptorTable(descriptorTable, group, groupData);
            }

            if (m_unboundedArrayCount)
            {
                UpdateUnboundedArrayDescriptorTables(group, groupData);
            }

            if (m_samplersDescriptorTableSize)
            {
                const DescriptorTable descriptorTable(
                    group.m_samplersDescriptorTable.GetOffset() + group.m_compiledDataIndex * m_samplersDescriptorTableSize,
                    static_cast<uint16_t>(m_samplersDescriptorTableSize));

                UpdateSamplersDescriptorTable(descriptorTable, groupBase, groupData);
            }

            return RHI::ResultCode::Success;
        }

        void ShaderResourceGroupPool::CacheGpuHandlesForViews(ShaderResourceGroup& group)
        {
            for (uint32_t i = 0; i < RHI::Limits::Device::FrameCountMax; ++i)
            {
                const DescriptorHandle descriptorHandle = group.m_viewsDescriptorTable.GetOffset() + m_viewsDescriptorTableSize * i;

                ShaderResourceGroupCompiledData& compiledData = group.m_compiledData[i];
                compiledData.m_gpuViewsDescriptorHandle = m_descriptorContext->GetGpuPlatformHandleForTable(
                    DescriptorTable(descriptorHandle, static_cast<uint16_t>(m_viewsDescriptorTableSize)));
            }
        }

        void ShaderResourceGroupPool::UpdateViewsDescriptorTable(DescriptorTable descriptorTable,
                                                                 RHI::DeviceShaderResourceGroup& group,
                                                                 const RHI::DeviceShaderResourceGroupData& groupData,
                                                                 bool forceUpdateViews /*= false*/ )
        {
            typedef AZ::RHI::DeviceShaderResourceGroupData::ResourceTypeMask ResourceMask;
            const RHI::ShaderResourceGroupLayout& groupLayout = *groupData.GetLayout();
            uint32_t shaderInputIndex = 0;
            
            if (forceUpdateViews || group.IsResourceTypeEnabledForCompilation(static_cast<uint32_t>(ResourceMask::BufferViewMask)))
            {
                for (const RHI::ShaderInputBufferDescriptor& shaderInputBuffer : groupLayout.GetShaderInputListForBuffers())
                {
                    const RHI::ShaderInputBufferIndex bufferInputIndex(shaderInputIndex);

                    AZStd::span<const RHI::ConstPtr<RHI::DeviceBufferView>> bufferViews = groupData.GetBufferViewArray(bufferInputIndex);
                    D3D12_DESCRIPTOR_RANGE_TYPE descriptorRangeType = ConvertShaderInputBufferAccess(shaderInputBuffer.m_access);
                    AZStd::small_vector<DescriptorHandle, SRGViewsFixedSize> descriptorHandles;
                    switch (descriptorRangeType)
                    {
                        case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
                        {
                            GetSRVsFromImageViews<RHI::DeviceBufferView, BufferView>(
                                bufferViews, D3D12_SRV_DIMENSION_BUFFER, descriptorHandles);
                            break;
                        }
                        case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
                        {
                            GetUAVsFromImageViews<RHI::DeviceBufferView, BufferView>(
                                bufferViews, D3D12_UAV_DIMENSION_BUFFER, descriptorHandles);
                            break;
                        }
                        case D3D12_DESCRIPTOR_RANGE_TYPE_CBV:
                        {
                            GetCBVsFromBufferViews(bufferViews, descriptorHandles);
                            break;
                        }
                        default:
                            AZ_Assert(false, "Unhandled D3D12_DESCRIPTOR_RANGE_TYPE enumeration");
                            break;
                    }

                    UpdateDescriptorTableRange(descriptorTable, descriptorHandles.span(), bufferInputIndex);
                    ++shaderInputIndex;
                }
            }

            if (forceUpdateViews || group.IsResourceTypeEnabledForCompilation(static_cast<uint32_t>(ResourceMask::ImageViewMask)))
            {
                shaderInputIndex = 0;
                for (const RHI::ShaderInputImageDescriptor& shaderInputImage : groupLayout.GetShaderInputListForImages())
                {
                    const RHI::ShaderInputImageIndex imageInputIndex(shaderInputIndex);

                    AZStd::span<const RHI::ConstPtr<RHI::DeviceImageView>> imageViews = groupData.GetImageViewArray(imageInputIndex);
                    D3D12_DESCRIPTOR_RANGE_TYPE descriptorRangeType = ConvertShaderInputImageAccess(shaderInputImage.m_access);

                    AZStd::small_vector<DescriptorHandle, SRGViewsFixedSize> descriptorHandles;
                    switch (descriptorRangeType)
                    {
                        case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
                        {
                            GetSRVsFromImageViews<RHI::DeviceImageView, ImageView>(
                                imageViews, ConvertSRVDimension(shaderInputImage.m_type), descriptorHandles);
                            break;
                        }
                        case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
                        {
                            GetUAVsFromImageViews<RHI::DeviceImageView, ImageView>(
                                imageViews, ConvertUAVDimension(shaderInputImage.m_type), descriptorHandles);
                            break;
                        }
                        default:
                        AZ_Assert(false, "Unhandled D3D12_DESCRIPTOR_RANGE_TYPE enumeration");
                        break;
                    }

                    UpdateDescriptorTableRange(descriptorTable, descriptorHandles.span(), imageInputIndex);
                    ++shaderInputIndex;
                }
            }
        }

        void ShaderResourceGroupPool::UpdateSamplersDescriptorTable(
            DescriptorTable descriptorTable,
            RHI::DeviceShaderResourceGroup& group,
            const RHI::DeviceShaderResourceGroupData& groupData)
        {
            if(group.IsResourceTypeEnabledForCompilation(static_cast<uint32_t>(RHI::DeviceShaderResourceGroupData::ResourceTypeMask::SamplerMask)))
            {
                const RHI::ShaderResourceGroupLayout& groupLayout = *groupData.GetLayout();
                const size_t shaderInputSize = groupLayout.GetShaderInputListForSamplers().size();
                for (size_t shaderInputIndex = 0; shaderInputIndex < shaderInputSize; ++shaderInputIndex)
                {
                    const RHI::ShaderInputSamplerIndex samplerInputIndex(shaderInputIndex);

                    AZStd::span<const RHI::SamplerState> samplers = groupData.GetSamplerArray(samplerInputIndex);
                    UpdateDescriptorTableRange(descriptorTable, samplerInputIndex, samplers);
                }
            }
        }

        void ShaderResourceGroupPool::UpdateUnboundedArrayDescriptorTables(ShaderResourceGroup& group, const RHI::DeviceShaderResourceGroupData& groupData)
        {
            const RHI::ShaderResourceGroupLayout& groupLayout = *groupData.GetLayout();
            uint32_t shaderInputIndex = 0;

            bool updateBuffers = group.IsResourceTypeEnabledForCompilation(
                static_cast<uint32_t>(RHI::DeviceShaderResourceGroupData::ResourceTypeMask::BufferViewUnboundedArrayMask));

            if (updateBuffers)
            {
                // process buffer unbounded arrays
                for (const RHI::ShaderInputBufferUnboundedArrayDescriptor& shaderInputBufferUnboundedArray : groupLayout.GetShaderInputListForBufferUnboundedArrays())
                {
                    const RHI::ShaderInputBufferUnboundedArrayIndex bufferUnboundedArrayInputIndex(shaderInputIndex);
                    uint32_t tableIndex = shaderInputIndex * RHI::Limits::Device::FrameCountMax + group.m_compiledDataIndex;
                    ShaderResourceGroupCompiledData& compiledData = group.m_compiledData[group.m_compiledDataIndex];

                    AZStd::span<const RHI::ConstPtr<RHI::DeviceBufferView>> bufferViews = groupData.GetBufferViewUnboundedArray(bufferUnboundedArrayInputIndex);

                    // resize the descriptor table allocation if necessary
                    if (group.m_unboundedDescriptorTables[tableIndex].GetSize() != bufferViews.size())
                    {
                        if (group.m_unboundedDescriptorTables[tableIndex].IsValid())
                        {
                            m_descriptorContext->ReleaseDescriptorTable(group.m_unboundedDescriptorTables[tableIndex]);
                            group.m_unboundedDescriptorTables[tableIndex] = DescriptorTable{};
                        }

                        if (!bufferViews.empty())
                        {
                            group.m_unboundedDescriptorTables[tableIndex] = m_descriptorContext->CreateDescriptorTable(
                                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, static_cast<uint32_t>(bufferViews.size()));
                            
                            if (!group.m_unboundedDescriptorTables[tableIndex].IsValid())
                            {
                                // It is possible to run out of number of descriptors in the descriptor heap if you are using custom SRG
                                // with an unbounded array as it can fragment over time. Consider using Bindless SRG's unbounded arrays as
                                // they do not fragment.
                                AZ_Assert(
                                    false,
                                    "Descriptor heap ran out of memory. Please consider increasing number of handles allowed for the "
                                    "second value of DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV within platformlimits.azasset file for dx12.");
                                return;
                            }

                            compiledData.m_gpuUnboundedArraysDescriptorHandles[shaderInputIndex] = m_descriptorContext->GetGpuPlatformHandleForTable(group.m_unboundedDescriptorTables[tableIndex]);
                        }
                    }
                    
                    const DescriptorTable descriptorTable(group.m_unboundedDescriptorTables[tableIndex].GetOffset(), static_cast<uint16_t>(bufferViews.size()));
                    UpdateUnboundedBuffersDescTable(descriptorTable, groupData, shaderInputIndex, shaderInputBufferUnboundedArray.m_access);
                    ++shaderInputIndex;
                }
            }

            bool updateImages = group.IsResourceTypeEnabledForCompilation(
                static_cast<uint32_t>(RHI::DeviceShaderResourceGroupData::ResourceTypeMask::ImageViewUnboundedArrayMask));

            if (updateImages)
            {
                // process image unbounded arrays
                for (const RHI::ShaderInputImageUnboundedArrayDescriptor& shaderInputImageUnboundedArray : groupLayout.GetShaderInputListForImageUnboundedArrays())
                {
                    const RHI::ShaderInputImageUnboundedArrayIndex imageUnboundedArrayInputIndex(shaderInputIndex);
                    uint32_t tableIndex = shaderInputIndex * RHI::Limits::Device::FrameCountMax + group.m_compiledDataIndex;
                    ShaderResourceGroupCompiledData& compiledData = group.m_compiledData[group.m_compiledDataIndex];

                    AZStd::span<const RHI::ConstPtr<RHI::DeviceImageView>> imageViews = groupData.GetImageViewUnboundedArray(imageUnboundedArrayInputIndex);

                    // resize the descriptor table allocation if necessary
                    if (group.m_unboundedDescriptorTables[tableIndex].GetSize() != imageViews.size())
                    {
                       
                        if (group.m_unboundedDescriptorTables[tableIndex].IsValid())
                        {
                            m_descriptorContext->ReleaseDescriptorTable(group.m_unboundedDescriptorTables[tableIndex]);
                            group.m_unboundedDescriptorTables[tableIndex] = DescriptorTable{};
                        }

                        if (!imageViews.empty())
                        {
                            group.m_unboundedDescriptorTables[tableIndex] = m_descriptorContext->CreateDescriptorTable(
                                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, static_cast<uint32_t>(imageViews.size()));

                            if (!group.m_unboundedDescriptorTables[tableIndex].IsValid())
                            {
                                // It is possible to run out of number of descriptors in the descriptor heap if you are using custom SRG with an unbounded array
                                // as it can fragment over time. Consider using Bindless SRG's unbounded arrays as they do not fragment.
                                AZ_Assert(
                                    false,
                                    "Descriptor heap ran out of memory. Please consider increasing number of handles allowed for the "
                                    "second value of DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV within platformlimits.azasset file for dx12.");
                                return;
                            }

                            compiledData.m_gpuUnboundedArraysDescriptorHandles[shaderInputIndex] = m_descriptorContext->GetGpuPlatformHandleForTable(group.m_unboundedDescriptorTables[tableIndex]);
                        }
                    }

                    const DescriptorTable descriptorTable(group.m_unboundedDescriptorTables[tableIndex].GetOffset(), static_cast<uint16_t>(imageViews.size()));
                    UpdateUnboundedImagesDescTable(descriptorTable, groupData, shaderInputIndex, shaderInputImageUnboundedArray.m_access, shaderInputImageUnboundedArray.m_type);
                    ++shaderInputIndex;
                }
            }
        }

        void ShaderResourceGroupPool::UpdateUnboundedBuffersDescTable(
            DescriptorTable descriptorTable,
            const RHI::DeviceShaderResourceGroupData& groupData,
            uint32_t shaderInputIndex,
            RHI::ShaderInputBufferAccess bufferAccess)
        {
            const RHI::ShaderInputBufferUnboundedArrayIndex bufferUnboundedArrayInputIndex(shaderInputIndex);
            AZStd::span<const RHI::ConstPtr<RHI::DeviceBufferView>> bufferViews =
                groupData.GetBufferViewUnboundedArray(bufferUnboundedArrayInputIndex);

            if (bufferViews.empty())
            {
                // we don't need to update descriptors since the buffer list is empty
                return;
            }

            D3D12_DESCRIPTOR_RANGE_TYPE descriptorRangeType = ConvertShaderInputBufferAccess(bufferAccess);

            AZStd::small_vector<DescriptorHandle, SRGViewsFixedSize> descriptorHandles;
            switch (descriptorRangeType)
            {
            case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
                {
                    GetSRVsFromImageViews<RHI::DeviceBufferView, BufferView>(bufferViews, D3D12_SRV_DIMENSION_BUFFER, descriptorHandles);
                    break;
                }
            case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
                {
                    GetUAVsFromImageViews<RHI::DeviceBufferView, BufferView>(bufferViews, D3D12_UAV_DIMENSION_BUFFER, descriptorHandles);
                    break;
                }
            default:
                AZ_Assert(false, "Unhandled D3D12_DESCRIPTOR_RANGE_TYPE enumeration");
                break;
            }

            m_descriptorContext->UpdateDescriptorTableRange(
                descriptorTable, descriptorHandles.span().data(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }

        void ShaderResourceGroupPool::UpdateUnboundedImagesDescTable(
            DescriptorTable descriptorTable,
            const RHI::DeviceShaderResourceGroupData& groupData,
            uint32_t shaderInputIndex,
            RHI::ShaderInputImageAccess imageAccess,
            RHI::ShaderInputImageType imageType)
        {
            const RHI::ShaderInputImageUnboundedArrayIndex imageUnboundedArrayInputIndex(shaderInputIndex);
            AZStd::span<const RHI::ConstPtr<RHI::DeviceImageView>> imageViews =
                groupData.GetImageViewUnboundedArray(imageUnboundedArrayInputIndex);

            if (imageViews.empty())
            {
                // we don't need to update descriptors since the image list is empty
                return;
            }

            D3D12_DESCRIPTOR_RANGE_TYPE descriptorRangeType = ConvertShaderInputImageAccess(imageAccess);

            AZStd::small_vector<DescriptorHandle, SRGViewsFixedSize> descriptorHandles;
            switch (descriptorRangeType)
            {
            case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
                {
                    GetSRVsFromImageViews<RHI::DeviceImageView, ImageView>(imageViews, ConvertSRVDimension(imageType), descriptorHandles);
                    break;
                }
            case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
                {
                    GetUAVsFromImageViews<RHI::DeviceImageView, ImageView>(imageViews, ConvertUAVDimension(imageType), descriptorHandles);
                    break;
                }
            default:
                AZ_Assert(false, "Unhandled D3D12_DESCRIPTOR_RANGE_TYPE enumeration");
                break;
            }

            m_descriptorContext->UpdateDescriptorTableRange(
                descriptorTable, descriptorHandles.span().data(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }

        void ShaderResourceGroupPool::OnFrameEnd()
        {
            m_constantAllocator.GarbageCollect();
            Base::OnFrameEnd();
        }

        DescriptorTable ShaderResourceGroupPool::GetBufferTable(DescriptorTable descriptorTable, RHI::ShaderInputBufferIndex bufferInputIndex) const
        {
            const RHI::Interval interval = GetLayout()->GetGroupInterval(bufferInputIndex);
            const DescriptorHandle startHandle = descriptorTable[m_descriptorTableBufferOffset + interval.m_min];
            return DescriptorTable(startHandle, aznumeric_caster(interval.m_max - interval.m_min));
        }

        DescriptorTable ShaderResourceGroupPool::GetImageTable(DescriptorTable descriptorTable, RHI::ShaderInputImageIndex imageInputIndex) const
        {
            const RHI::Interval interval = GetLayout()->GetGroupInterval(imageInputIndex);
            const DescriptorHandle startHandle = descriptorTable[m_descriptorTableImageOffset + interval.m_min];
            return DescriptorTable(startHandle, aznumeric_caster(interval.m_max - interval.m_min));
        }

        DescriptorTable ShaderResourceGroupPool::GetSamplerTable(DescriptorTable descriptorTable, RHI::ShaderInputSamplerIndex samplerInputIndex) const
        {
            const RHI::Interval interval = GetLayout()->GetGroupInterval(samplerInputIndex);
            const DescriptorHandle startHandle = descriptorTable[interval.m_min];
            return DescriptorTable(startHandle, aznumeric_caster(interval.m_max - interval.m_min));
        }

        void ShaderResourceGroupPool::UpdateDescriptorTableRange(
            DescriptorTable descriptorTable, const AZStd::span<DescriptorHandle>& handles, RHI::ShaderInputBufferIndex bufferInputIndex)
        {
            const DescriptorTable gpuDestinationTable = GetBufferTable(descriptorTable, bufferInputIndex);
            m_descriptorContext->UpdateDescriptorTableRange(gpuDestinationTable, handles.data(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }

        void ShaderResourceGroupPool::UpdateDescriptorTableRange(
            DescriptorTable descriptorTable, const AZStd::span<DescriptorHandle>& handles, RHI::ShaderInputImageIndex imageInputIndex)
        {
            const DescriptorTable gpuDestinationTable = GetImageTable(descriptorTable, imageInputIndex);
            m_descriptorContext->UpdateDescriptorTableRange(gpuDestinationTable, handles.data(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }

        void ShaderResourceGroupPool::UpdateDescriptorTableRange(
            DescriptorTable descriptorTable,
            RHI::ShaderInputSamplerIndex samplerInputIndex, 
            AZStd::span<const RHI::SamplerState> samplerStates)
        {
            const DescriptorHandle nullHandle = m_descriptorContext->GetNullHandleSampler();
            AZStd::small_vector<DescriptorHandle, SRGViewsFixedSize> cpuSourceDescriptors(
                aznumeric_caster(samplerStates.size()), nullHandle);
            auto& device = static_cast<Device&>(GetDevice());
            AZStd::small_vector<RHI::ConstPtr<Sampler>, SRGViewsFixedSize> samplers(samplerStates.size(), nullptr);
            for (size_t i = 0; i < samplerStates.size(); ++i)
            {
                samplers.span()[i] = device.AcquireSampler(samplerStates[i]);
                cpuSourceDescriptors.span()[i] = samplers.span()[i]->GetDescriptorHandle();
            }

            const DescriptorTable gpuDestinationTable = GetSamplerTable(descriptorTable, samplerInputIndex);
            m_descriptorContext->UpdateDescriptorTableRange(
                gpuDestinationTable, cpuSourceDescriptors.span().data(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
        }
    }
}
