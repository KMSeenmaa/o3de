/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#include <Atom/RPI.Public/Shader/Shader.h>

#include <Atom/RHI/Factory.h>
#include <Atom/RHI/PipelineStateCache.h>
#include <Atom/RHI/RHISystemInterface.h>
#include <AtomCore/Instance/InstanceDatabase.h>
#include <Atom/RPI.Public/Shader/ShaderReloadDebugTracker.h>
#include <Atom/RPI.Public/Shader/ShaderSystemInterface.h>
#include <Atom/RPI.Public/Shader/ShaderResourceGroup.h>
#include <AzCore/Interface/Interface.h>
#include <AzCore/std/time.h>

#include <AzCore/Component/TickBus.h>

#define PSOCacheVersion 0 // Bump this if you want to reset PSO cache for everyone

namespace AZ
{
    namespace RPI
    {
        Data::Instance<Shader> Shader::FindOrCreate(const Data::Asset<ShaderAsset>& shaderAsset, const Name& supervariantName)
        {
            auto anySupervariantName = AZStd::any(supervariantName);

            // retrieve the supervariant index from the shader asset
            SupervariantIndex supervariantIndex = shaderAsset->GetSupervariantIndex(supervariantName);
            if (!supervariantIndex.IsValid())
            {
                AZ_Error("Shader", false, "Supervariant with name %s, was not found in shader %s", supervariantName.GetCStr(), shaderAsset->GetName().GetCStr());
                return nullptr;
            }

            // Create the instance ID using the shader asset with an additional unique identifier from the Super variant index.
            const Data::InstanceId instanceId =
                Data::InstanceId::CreateFromAsset(shaderAsset, { supervariantIndex.GetIndex() });

            // retrieve the shader instance from the Instance database
            return Data::InstanceDatabase<Shader>::Instance().FindOrCreate(instanceId, shaderAsset, &anySupervariantName);
        }

        Data::Instance<Shader> Shader::FindOrCreate(const Data::Asset<ShaderAsset>& shaderAsset)
        {
            return FindOrCreate(shaderAsset, AZ::Name{ "" });
        }

        Data::Instance<Shader> Shader::CreateInternal([[maybe_unused]] ShaderAsset& shaderAsset, const AZStd::any* anySupervariantName)
        {
            AZ_Assert(anySupervariantName != nullptr, "Invalid supervariant name param");
            auto supervariantName = AZStd::any_cast<AZ::Name>(*anySupervariantName);
            auto supervariantIndex = shaderAsset.GetSupervariantIndex(supervariantName);
            if (!supervariantIndex.IsValid())
            {
                AZ_Error("Shader", false, "Supervariant with name %s, was not found in shader %s", supervariantName.GetCStr(), shaderAsset.GetName().GetCStr());
                return nullptr;
            }

            Data::Instance<Shader> shader = aznew Shader(supervariantIndex);
            const RHI::ResultCode resultCode = shader->Init(shaderAsset);
            if (resultCode != RHI::ResultCode::Success)
            {
                return nullptr;
            }
            return shader;
        }

        Shader::~Shader()
        {
            Shutdown();
        }

        static bool GetPipelineLibraryPaths(
            AZStd::unordered_map<int, AZStd::string>& pipelineLibraryPaths,
            size_t pipelineLibraryPathLength,
            const ShaderAsset& shaderAsset)
        {
            if (auto* fileIOBase = IO::FileIOBase::GetInstance())
            {
                const Data::AssetId& assetId = shaderAsset.GetId();

                Name platformName = RHI::Factory::Get().GetName();
                Name shaderName = shaderAsset.GetName();

                AZStd::string uuidString;
                assetId.m_guid.ToString<AZStd::string>(uuidString, false, false);

                AZStd::string configString;
                if (RHI::BuildOptions::IsDebugBuild)
                {
                    configString = "Debug";
                }
                else if (RHI::BuildOptions::IsProfileBuild)
                {
                    configString = "Profile";
                }
                else
                {
                    configString = "Release";
                }

                auto deviceCount = RHI::RHISystemInterface::Get()->GetDeviceCount();

                for (int deviceIndex = 0; deviceIndex < deviceCount; ++deviceIndex)
                {
                    RHI::PhysicalDeviceDescriptor devicePhysicalDeviceDesc{
                        RHI::RHISystemInterface::Get()->GetDevice(deviceIndex)->GetPhysicalDevice().GetDescriptor()
                    };
                    char pipelineLibraryPathTemp[AZ_MAX_PATH_LEN];
                    azsnprintf(
                        pipelineLibraryPathTemp,
                        AZ_MAX_PATH_LEN,
                        "@user@/Atom/PipelineStateCache_%s_%u_%u_%s_Ver_%i/%s/%s_%s_%d",
                        ToString(devicePhysicalDeviceDesc.m_vendorId).data(),
                        devicePhysicalDeviceDesc.m_deviceId,
                        devicePhysicalDeviceDesc.m_driverVersion,
                        configString.data(),
                        PSOCacheVersion,
                        platformName.GetCStr(),
                        shaderName.GetCStr(),
                        uuidString.data(),
                        assetId.m_subId);

                    char resolvedPipelineLibraryPath[AZ_MAX_PATH_LEN];
                    fileIOBase->ResolvePath(pipelineLibraryPathTemp, resolvedPipelineLibraryPath, pipelineLibraryPathLength);
                    pipelineLibraryPaths[deviceIndex] = resolvedPipelineLibraryPath;
                }
                return true;
            }
            return false;
        }

        RHI::ResultCode Shader::Init(ShaderAsset& shaderAsset)
        {
            Data::AssetBus::MultiHandler::BusDisconnect();
            ShaderVariantFinderNotificationBus::Handler::BusDisconnect();

            RHI::RHISystemInterface* rhiSystem = RHI::RHISystemInterface::Get();
            RHI::DrawListTagRegistry* drawListTagRegistry = rhiSystem->GetDrawListTagRegistry();

            m_asset = { &shaderAsset, AZ::Data::AssetLoadBehavior::PreLoad };
            m_pipelineStateType = shaderAsset.GetPipelineStateType();

            GetPipelineLibraryPaths(m_pipelineLibraryPaths, AZ_MAX_PATH_LEN, *m_asset);

            {
                AZStd::unique_lock<decltype(m_variantCacheMutex)> lock(m_variantCacheMutex);
                m_shaderVariants.clear();
            }
            auto rootShaderVariantAsset = shaderAsset.GetRootVariantAsset(m_supervariantIndex);
            m_rootVariant.Init(m_asset, rootShaderVariantAsset, m_supervariantIndex);

            if (m_pipelineLibraryHandle.IsNull())
            {
                // We set up a pipeline library only once for the lifetime of the Shader instance.
                // This should allow the Shader to be reloaded at runtime many times, and cache and reuse PipelineState objects rather than rebuild them.
                // It also fixes a particular TDR crash that occurred on some hardware when hot-reloading shaders and building pipeline states
                // in a new pipeline library every time.

                RHI::PipelineStateCache* pipelineStateCache = rhiSystem->GetPipelineStateCache();
                auto serializedData = LoadPipelineLibrary();
                RHI::PipelineLibraryHandle pipelineLibraryHandle =
                    pipelineStateCache->CreateLibrary(serializedData, m_pipelineLibraryPaths);

                if (pipelineLibraryHandle.IsNull())
                {
                    AZ_Error("Shader", false, "Failed to create pipeline library from pipeline state cache.");
                    return RHI::ResultCode::Fail;
                }

                m_pipelineLibraryHandle = pipelineLibraryHandle;
                m_pipelineStateCache = pipelineStateCache;
            }

            const Name& drawListName = shaderAsset.GetDrawListName();
            if (!drawListName.IsEmpty())
            {
                m_drawListTag = drawListTagRegistry->AcquireTag(drawListName);
                if (!m_drawListTag.IsValid())
                {
                    AZ_Error("Shader", false, "Failed to acquire a DrawListTag. Entries are full.");
                }
            }

            ShaderVariantFinderNotificationBus::Handler::BusConnect(m_asset.GetId());

            m_reloadedAssets.clear();
            const auto& supervariants = m_asset->GetCurrentShaderApiData().m_supervariants;
            m_expectedAssetReloadCount = 1 /*m_asset*/ + supervariants.size();
            Data::AssetBus::MultiHandler::BusConnect(m_asset.GetId());
            for (const auto& supervariant : supervariants)
            {
                Data::AssetBus::MultiHandler::BusConnect(supervariant.m_rootShaderVariantAsset.GetId());
            }
            return RHI::ResultCode::Success;
        }

        void Shader::Shutdown()
        {
            ShaderVariantFinderNotificationBus::Handler::BusDisconnect();
            Data::AssetBus::MultiHandler::BusDisconnect();

            if (m_pipelineLibraryHandle.IsValid())
            {
                if (r_enablePsoCaching)
                {
                    SavePipelineLibrary();
                }
                
                m_pipelineStateCache->ReleaseLibrary(m_pipelineLibraryHandle);
                m_pipelineStateCache = nullptr;
                m_pipelineLibraryHandle = {};
            }

            if (m_drawListTag.IsValid())
            {
                RHI::DrawListTagRegistry* drawListTagRegistry = RHI::RHISystemInterface::Get()->GetDrawListTagRegistry();
                drawListTagRegistry->ReleaseTag(m_drawListTag);
                m_drawListTag.Reset();
            }
        }

        ///////////////////////////////////////////////////////////////////////
        // AssetBus overrides
        void Shader::OnAssetReloaded(Data::Asset<Data::AssetData> asset)
        {
            ShaderReloadDebugTracker::ScopedSection reloadSection("{%p}->Shader::OnAssetReloaded %s.\n",
                this, asset.GetHint().c_str());

            m_reloadedAssets.emplace(asset.GetId(), asset);

            if (ShaderReloadDebugTracker::IsEnabled())
            {
                ShaderReloadDebugTracker::Printf(
                    "Current ShaderAssetPtr={%p} with RootVariantAssetPtr={%p}", m_asset.Get(), m_asset->GetRootVariantAsset().Get());

                ShaderReloadDebugTracker::Printf("{%p} -> Shader::OnAssetReloaded so far only %zu of %zu assets have been reloaded.",
                    this, m_reloadedAssets.size(), m_expectedAssetReloadCount);

                AZStd::sys_time_t now = AZStd::GetTimeUTCMilliSecond();
                if (asset.GetType() == AZ::AzTypeInfo<ShaderVariantAsset>::Uuid())
                {
                    ShaderReloadDebugTracker::Printf(
                        "{%p}->Shader::OnRootVariantReloaded [current time %lld] got new variant {%p}'%s'",
                        this,
                        now,
                        asset.Get(),
                        asset.GetHint().c_str());
                }
                else
                {
                    const auto* newShaderAsset = asset.GetAs<ShaderAsset>();
                    const auto shaderVariantAsset = newShaderAsset->GetRootVariantAsset();
                    ShaderReloadDebugTracker::Printf(
                        "{%p}->Shader::OnShaderAssetReloaded [current time %lld] got new shader {%p}'%s' with included variant {%p}'%s'",
                        this,
                        now,
                        newShaderAsset,
                        asset.GetHint().c_str(),
                        shaderVariantAsset.Get(),
                        shaderVariantAsset.GetHint().c_str());
                }
            }

            if (m_reloadedAssets.size() != m_expectedAssetReloadCount)
            {
                return;
            }

            // Time to update all references:
            auto itor = m_reloadedAssets.find(m_asset.GetId());
            if (itor == m_reloadedAssets.end())
            {
                AZ_Error("Shader", false, "Can not find the reloaded ShaderAsset with ID '%s'. Hint '%s'",
                    m_asset.GetId().ToString<AZStd::string>().c_str(),
                    m_asset.GetHint().c_str());
                return;
            }

            m_asset = itor->second;
            m_reloadedAssets.erase(itor);
            for (auto& [assetId, rootVariantAsset] : m_reloadedAssets)
            {
                AZ_Assert(rootVariantAsset.GetType() == AZ::AzTypeInfo<ShaderVariantAsset>::Uuid(),
                    "Was expecting only ShaderVariantAsset(s)");
                if (!m_asset->UpdateRootShaderVariantAsset(Data::static_pointer_cast<ShaderVariantAsset>(rootVariantAsset)))
                {
                    AZ_Error("Shader", false,
                        "Failed to update Root ShaderVariantAsset {%p}'%s'",
                        rootVariantAsset.Get(),
                        rootVariantAsset.GetHint().c_str());
                }
            }
            m_reloadedAssets.clear();

            Init(*m_asset.Get());
            ShaderReloadNotificationBus::Event(asset.GetId(), &ShaderReloadNotificationBus::Events::OnShaderReinitialized, *this);
        }
        ///////////////////////////////////////////////////////////////////////

        ///////////////////////////////////////////////////////////////////
        /// ShaderVariantFinderNotificationBus overrides
        void Shader::OnShaderVariantAssetReady(Data::Asset<ShaderVariantAsset> shaderVariantAsset, bool isError)
        {
            ShaderReloadDebugTracker::ScopedSection reloadSection("{%p}->Shader::OnShaderVariantAssetReady %s", this, shaderVariantAsset.GetHint().c_str());

            AZ_Assert(shaderVariantAsset, "Reloaded ShaderVariantAsset is null");
            const ShaderVariantStableId stableId = shaderVariantAsset->GetStableId();

            // check the supervariantIndex of the ShaderVariantAsset to make sure it matches the supervariantIndex of this shader instance
            if (shaderVariantAsset->GetSupervariantIndex() != m_supervariantIndex.GetIndex())
            {
                return;
            }

            // We make a copy of the updated variant because OnShaderVariantReinitialized must not be called inside
            // m_variantCacheMutex or deadlocks may occur.
            // Or if there is an error, we leave this object in its default state to indicate there was an error.
            // [GFX TODO] We really should have a dedicated message/event for this, but that will be covered by a future task where
            // we will merge ShaderReloadNotificationBus messages into one. For now, we just indicate the error by passing an empty ShaderVariant,
            // all our call sites don't use this data anyway.
            ShaderVariant updatedVariant;

            if (isError)
            {
                //Remark: We do not assert if the stableId == RootShaderVariantStableId, because we can not trust in the asset data
                //on error. so it is possible that on error the stbleId == RootShaderVariantStableId;
                if (stableId == RootShaderVariantStableId)
                {
                    return;
                }
                AZStd::unique_lock<decltype(m_variantCacheMutex)> lock(m_variantCacheMutex);
                m_shaderVariants.erase(stableId);
            }
            else
            {
                AZ_Assert(stableId != RootShaderVariantStableId,
                    "The root variant is expected to be updated by the ShaderAsset.");
                AZStd::unique_lock<decltype(m_variantCacheMutex)> lock(m_variantCacheMutex);

                auto iter = m_shaderVariants.find(stableId);
                if (iter != m_shaderVariants.end())
                {
                    ShaderVariant& shaderVariant = iter->second;

                    if (!shaderVariant.Init(m_asset, shaderVariantAsset, m_supervariantIndex))
                    {
                        AZ_Error("Shader", false, "Failed to init shaderVariant with StableId=%u", shaderVariantAsset->GetStableId());
                        m_shaderVariants.erase(stableId);
                    }
                    else
                    {
                        updatedVariant = shaderVariant;
                    }
                }
                else
                {
                    //This is the first time the shader variant asset comes to life.
                    updatedVariant.Init(m_asset, shaderVariantAsset, m_supervariantIndex);
                    m_shaderVariants.emplace(stableId, updatedVariant);
                }
            }

            // [GFX TODO] It might make more sense to call OnShaderReinitialized here
            ShaderReloadNotificationBus::Event(m_asset.GetId(), &ShaderReloadNotificationBus::Events::OnShaderVariantReinitialized, updatedVariant);
        }
        ///////////////////////////////////////////////////////////////////

        AZStd::unordered_map<int, ConstPtr<RHI::PipelineLibraryData>> Shader::LoadPipelineLibrary() const
        {
            AZStd::unordered_map<int, ConstPtr<RHI::PipelineLibraryData>> pipelineLibraries;
            auto deviceCount = RHI::RHISystemInterface::Get()->GetDeviceCount();

            for (int deviceIndex = 0; deviceIndex < deviceCount; ++deviceIndex)
            {
                pipelineLibraries[deviceIndex] =
                    Utils::LoadObjectFromFile<RHI::PipelineLibraryData>(m_pipelineLibraryPaths.at(deviceIndex));
            }

            return pipelineLibraries;
        }

        void Shader::SavePipelineLibrary() const
        {
            if (!m_pipelineLibraryPaths.empty())
            {
                RHI::ConstPtr<RHI::PipelineLibrary> pipelineLibrary = m_pipelineStateCache->GetMergedLibrary(m_pipelineLibraryHandle);
                if (!pipelineLibrary)
                {
                    return;
                }

                auto deviceCount = RHI::RHISystemInterface::Get()->GetDeviceCount();

                for (int deviceIndex = 0; deviceIndex < deviceCount; ++deviceIndex)
                {
                    RHI::Device* device = RHI::RHISystemInterface::Get()->GetDevice(deviceIndex);

                    RHI::ConstPtr<RHI::DevicePipelineLibrary> pipelineLib = pipelineLibrary->GetDevicePipelineLibrary(deviceIndex);

                    // Check if explicit file load/save operation is needed as the RHI backend api may not support it
                    if (device->GetFeatures().m_isPsoCacheFileOperationsNeeded)
                    {
                        RHI::ConstPtr<RHI::PipelineLibraryData> serializedData = pipelineLib->GetSerializedData();
                        if (serializedData)
                        {
                            Utils::SaveObjectToFile<RHI::PipelineLibraryData>(
                                m_pipelineLibraryPaths.at(deviceIndex), DataStream::ST_BINARY, serializedData.get());
                        }
                    }
                    else
                    {
                        [[maybe_unused]] bool result = pipelineLib->SaveSerializedData(m_pipelineLibraryPaths.at(deviceIndex));
                        AZ_Error("Shader", result, "Pipeline Library %s was not saved", &m_pipelineLibraryPaths.at(deviceIndex));
                    }
                }
            }
        }
        
        ShaderOptionGroup Shader::CreateShaderOptionGroup() const
        {
            return ShaderOptionGroup(m_asset->GetShaderOptionGroupLayout());
        }

        const ShaderVariant& Shader::GetVariant(const ShaderVariantId& shaderVariantId)
        {
            Data::Asset<ShaderVariantAsset> shaderVariantAsset = m_asset->GetVariantAsset(shaderVariantId, m_supervariantIndex);
            if (!shaderVariantAsset || shaderVariantAsset->IsRootVariant())
            {
                return m_rootVariant;
            }

            return GetVariant(shaderVariantAsset->GetStableId());
        }

        const ShaderVariant& Shader::GetRootVariant()
        {
            return m_rootVariant;
        }

        const ShaderVariant& Shader::GetDefaultVariant()
        {
            ShaderOptionGroup defaultOptions = GetDefaultShaderOptions();
            return GetVariant(defaultOptions.GetShaderVariantId());
        }

        ShaderOptionGroup Shader::GetDefaultShaderOptions() const
        {
            return m_asset->GetDefaultShaderOptions();
        }

        ShaderVariantSearchResult Shader::FindVariantStableId(const ShaderVariantId& shaderVariantId) const
        {
            ShaderVariantSearchResult variantSearchResult = m_asset->FindVariantStableId(shaderVariantId);
            return variantSearchResult;
        }

        const ShaderVariant& Shader::GetVariant(ShaderVariantStableId shaderVariantStableId)
        {
            const ShaderVariant& variant = GetVariantInternal(shaderVariantStableId);
            
            if (ShaderReloadDebugTracker::IsEnabled())
            {
                AZStd::sys_time_t now = AZStd::GetTimeUTCMilliSecond();

                ShaderReloadDebugTracker::Printf("{%p}->Shader::GetVariant for shader '%s' [current time %lld] found variant '%s'",
                    this, m_asset.GetHint().c_str(), now, variant.GetShaderVariantAsset().GetHint().c_str());
            }

            return variant;
        }

        const ShaderVariant& Shader::GetVariantInternal(ShaderVariantStableId shaderVariantStableId)
        {
            if (!shaderVariantStableId.IsValid() || shaderVariantStableId == ShaderAsset::RootShaderVariantStableId)
            {
                return m_rootVariant;
            }

            {
                AZStd::shared_lock<decltype(m_variantCacheMutex)> lock(m_variantCacheMutex);

                auto findIt = m_shaderVariants.find(shaderVariantStableId);
                if (findIt != m_shaderVariants.end())
                {
                    return findIt->second;
                }
            }

            // By calling GetVariant, an asynchronous asset load request is enqueued if the variant
            // is not fully ready.
            Data::Asset<ShaderVariantAsset> shaderVariantAsset = m_asset->GetVariantAsset(shaderVariantStableId, m_supervariantIndex);
            if (!shaderVariantAsset || shaderVariantAsset == m_asset->GetRootVariantAsset())
            {
                // Return the root variant when the requested variant is not ready.
                return m_rootVariant;
            }

            AZStd::unique_lock<decltype(m_variantCacheMutex)> lock(m_variantCacheMutex);

            // For performance reasons We are breaking this function into two locking steps.
            // which means We must check again if the variant is already in the cache.
            auto findIt = m_shaderVariants.find(shaderVariantStableId);
            if (findIt != m_shaderVariants.end())
            {
                return findIt->second;
            }

            ShaderVariant newVariant;
            newVariant.Init(m_asset, shaderVariantAsset, m_supervariantIndex);
            m_shaderVariants.emplace(shaderVariantStableId, newVariant);

            return m_shaderVariants.at(shaderVariantStableId);
        }

        RHI::PipelineStateType Shader::GetPipelineStateType() const
        {
            return m_pipelineStateType;
        }

        const ShaderInputContract& Shader::GetInputContract() const
        {
            return m_asset->GetInputContract(m_supervariantIndex);
        }

        const ShaderOutputContract& Shader::GetOutputContract() const
        {
            return m_asset->GetOutputContract(m_supervariantIndex);
        }

        const RHI::PipelineState* Shader::AcquirePipelineState(const RHI::PipelineStateDescriptor& descriptor) const
        {
            return m_pipelineStateCache->AcquirePipelineState(m_pipelineLibraryHandle, descriptor, m_asset->GetName());
        }

        const RHI::Ptr<RHI::ShaderResourceGroupLayout>& Shader::FindShaderResourceGroupLayout(const Name& shaderResourceGroupName) const
        {
            return m_asset->FindShaderResourceGroupLayout(shaderResourceGroupName, m_supervariantIndex);
        }

        const RHI::Ptr<RHI::ShaderResourceGroupLayout>& Shader::FindShaderResourceGroupLayout(uint32_t bindingSlot) const
        {
            return m_asset->FindShaderResourceGroupLayout(bindingSlot, m_supervariantIndex);
        }

        const RHI::Ptr<RHI::ShaderResourceGroupLayout>& Shader::FindFallbackShaderResourceGroupLayout() const
        {
            return m_asset->FindFallbackShaderResourceGroupLayout(m_supervariantIndex);
        }

        AZStd::span<const RHI::Ptr<RHI::ShaderResourceGroupLayout>> Shader::GetShaderResourceGroupLayouts() const
        {
            return m_asset->GetShaderResourceGroupLayouts(m_supervariantIndex);
        }

        Data::Instance<ShaderResourceGroup> Shader::CreateDrawSrgForShaderVariant(const ShaderOptionGroup& shaderOptions, bool compileTheSrg)
        {
            RHI::Ptr<RHI::ShaderResourceGroupLayout> drawSrgLayout = m_asset->GetDrawSrgLayout(GetSupervariantIndex());
            Data::Instance<ShaderResourceGroup> drawSrg;
            if (drawSrgLayout)
            {
                drawSrg = RPI::ShaderResourceGroup::Create(m_asset, GetSupervariantIndex(), drawSrgLayout->GetName());
                bool useFallbackKey = !shaderOptions.GetShaderOptionLayout()->IsFullySpecialized() ||
                    !m_asset->UseSpecializationConstants(GetSupervariantIndex());
                if (useFallbackKey && drawSrgLayout->HasShaderVariantKeyFallbackEntry())
                {
                    drawSrg->SetShaderVariantKeyFallbackValue(shaderOptions.GetShaderVariantKeyFallbackValue());
                }

                if (compileTheSrg)
                {
                    drawSrg->Compile();
                }
            }

            return drawSrg;
        }

        Data::Instance<ShaderResourceGroup> Shader::CreateDefaultDrawSrg(bool compileTheSrg)
        {
            return CreateDrawSrgForShaderVariant(m_asset->GetDefaultShaderOptions(), compileTheSrg);
        }

        const Data::Asset<ShaderAsset>& Shader::GetAsset() const
        {
            return m_asset;
        }
        
        RHI::DrawListTag Shader::GetDrawListTag() const
        {
            return m_drawListTag;
        }

    } // namespace RPI
} // namespace AZ
