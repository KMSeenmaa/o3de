/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <Atom/RHI/StreamBufferView.h>

#include <Atom/Feature/Mesh/MeshFeatureProcessorInterface.h>

namespace AZ
{
    namespace Render
    {
        // An Interface that contains all Stream BufferViews (AZ::RHI::BufferView)
        // requested to @StreamBufferViewsBuilderInterface.
        // This is useful to manually bind Mesh Stream Buffers in a Compute or RayTracing Shader.
        class ShaderStreamBufferViewsInterface
        {
        public:
            AZ_RTTI(AZ::Render::ShaderStreamBufferViewsInterface, "{3A80C85C-DD3A-4A1D-B564-291EB463CD0B}");
            virtual ~ShaderStreamBufferViewsInterface() = default;

            //! Returns the Shader-bindable RHI::BufferView for the Vertex Indices from a particular mesh.
            //! REMARK: If the BindlessSrg is enabled, which typically is, this buffer view is already bound
            //! as a ByteAddressBuffer
            virtual const AZ::RHI::Ptr<AZ::RHI::BufferView>& GetVertexIndicesBufferView() const = 0;
            //! Returns the IndexBufferView from the Vertex Indices, which is useful to know the actual
            //! byte offset, byte count, etc related to the sub mesh(Mesh Index). 
            virtual const AZ::RHI::IndexBufferView& GetVertexIndicesIndexBufferView() const = 0;
            //! A shortcut to get the Read Index from the BindlessSrg for the IndexBufferView.
            //! Alternatively, you could get the same index by getting it directly from the AZ::RHI::BufferView
            //! returned by GetIndexBufferView()
            virtual uint32_t GetVertexIndicesBindlessReadIndex(int deviceIndex) const = 0;
            //! Same as above, but conveniently returns all the indices across all available devices
            //! in an unordered_map.
            virtual AZStd::unordered_map<int, uint32_t> GetVertexIndicesBindlessReadIndex() const = 0;

            //! Returns the Shader-bindable RHI::BufferView for a Vertex Stream, identifiable by @shaderSemantic, from a particular mesh.
            virtual const AZ::RHI::Ptr<AZ::RHI::BufferView>& GetBufferView(const AZ::RHI::ShaderSemantic& shaderSemantic) const = 0;
            //! Same as above, but provides the convenience of finding the vertex stream by name, like "POSITION" or "UV1", etc.
            virtual const AZ::RHI::Ptr<AZ::RHI::BufferView>& GetBufferView(const char* semanticName) const = 0;

            //! Returns the StreamBufferView for a Vertex Stream, identifiable by @shaderSemantic, from a particular mesh.
            //! This is useful to know the actual byte offset, byte count, etc related to the sub mesh(Mesh Index).
            //! if the semantic is not fund, it returns nullptr.
            virtual const AZ::RHI::StreamBufferView* GetStreamBufferView(const AZ::RHI::ShaderSemantic& shaderSemantic) const = 0;
            //! Same as above, but provides the convenience of finding the vertex stream by name, like "POSITION" or "UV1", etc.
            virtual const AZ::RHI::StreamBufferView* GetStreamBufferView(const char* semanticName) const = 0;

            //! A shortcut to get the Read Index from the BindlessSrg for a particular BufferView identifiable by its shader semantic.
            //! Alternatively, you could get the same index by getting it directly from the AZ::RHI::BufferView
            //! returned by GetBufferView().
            virtual uint32_t GetStreamBufferViewBindlessReadIndex(int deviceIndex, const AZ::RHI::ShaderSemantic& shaderSemantic) const = 0;
            virtual uint32_t GetStreamBufferViewBindlessReadIndex(int deviceIndex, const char* semanticName) const = 0;
            //! Same as above, but conveniently returns all the indices across all available devices
            //! in an unordered_map.
            virtual AZStd::unordered_map<int, uint32_t> GetStreamBufferViewBindlessReadIndex(
                const AZ::RHI::ShaderSemantic& shaderSemantic) const = 0;
            virtual AZStd::unordered_map<int, uint32_t> GetStreamBufferViewBindlessReadIndex(
                const char* semanticName) const = 0;

            //! For informational purposes. Returns the LOD Index of the Mesh.
            virtual uint32_t GetLodIndex() const = 0;
            // For informational purposes. Returns the Mesh Index (Within the current LOD index) of the Mesh.
            virtual uint32_t GetMeshIndex() const = 0;
        };


        // This helper interface is typically used to manually define a set of Stream Buffers, identifiable
        // by their Shader Semantics, like POSITION, NORMAL, UV0, UV1, etc... and create Buffer Views that can be bound
        // to a Shader (typically Compute or RayTracing, because Raster shaders get the streams automatically bound to the InputAssembly stage).
        // The most common use case is a non-Raster shader that uses the BindlessSrg and needs to know the indices
        // of each StreamBuffer within the BindlessSrg::m_ByteAddressBuffer.
        // To create one of these builders please use ModelDataInstanceInterface::CreateStreamBufferViewsBuilder().
        class StreamBufferViewsBuilderInterface
        {
        public:
            AZ_RTTI(AZ::Render::StreamBufferViewsBuilderInterface, "{B0004EA8-C829-427D-8F3B-0FBB060CB385}");
            virtual ~StreamBufferViewsBuilderInterface() = default;

            //! All streams that need to be queried must be added before calling BuildShaderStreamBufferViews();
            virtual bool AddStream(const char* semanticName, AZ::RHI::Format streamFormat, bool isOptional) = 0;

            //! Returns the number of streams that were successfully added via @AddStream(...)
            virtual AZ::u8 GetStreamCount() const = 0;

            //! If @AddStream() is never called, the returned  @ShaderStreamBufferViewsInterface may only be useful
            //! for GetIndexBufferView().
            //! The returned ShaderStreamBufferViewsInterface can only get the Streams on the particular
            //! @meshIndex within the @lodIndex.
            virtual AZStd::unique_ptr<ShaderStreamBufferViewsInterface> BuildShaderStreamBufferViews(
                uint32_t lodIndex, uint32_t meshIndex) = 0;

            //! For informational purposes. Gets a reference to the MeshHandle used when this builder was created.
            virtual const MeshFeatureProcessorInterface::MeshHandle& GetMeshHandle() const = 0;
        };

    } // namespace Render
} // namespace AZ
