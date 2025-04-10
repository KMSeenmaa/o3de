/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzCore/Serialization/SerializeContext.h>

#include "BoolTrack.h"
#include "Maestro/Types/AnimValueType.h"

namespace Maestro
{

    CBoolTrack::CBoolTrack()
        : m_bDefaultValue(true)
    {
    }

    void CBoolTrack::GetKeyInfo([[maybe_unused]] int keyIndex, const char*& description, float& duration) const
    {
        description = 0;
        duration = 0;
    }

    AnimValueType CBoolTrack::GetValueType() const
    {
        return AnimValueType::Bool;
    }

    void CBoolTrack::GetValue(float time, bool& value) const
    {
        value = m_bDefaultValue;

        const int numKeys = GetNumKeys();
        if (numKeys < 1)
        {
            return;
        }

        int keyIdx = 0;
        while ((keyIdx < numKeys) && (time >= m_keys[keyIdx].time))
        {
            keyIdx++;
        }

        if (m_bDefaultValue)
        {
            value = !(keyIdx & 1); // True if even key.
        }
        else
        {
            value = (keyIdx & 1); // False if even key.
        }
    }

    void CBoolTrack::SetValue([[maybe_unused]] float time, const bool& value, bool bDefault)
    {
        if (bDefault)
        {
            SetDefaultValue(value);
        }
    }

    void CBoolTrack::SetDefaultValue(const bool bDefaultValue)
    {
        m_bDefaultValue = bDefaultValue;
    }

    /// @deprecated Serialization for Sequence data in Component Entity Sequences now occurs through AZ::SerializeContext and the Sequence
    /// Component
    bool CBoolTrack::Serialize(XmlNodeRef& xmlNode, bool bLoading, bool bLoadEmptyTracks)
    {
        bool retVal = TAnimTrack<IBoolKey>::Serialize(xmlNode, bLoading, bLoadEmptyTracks);
        if (bLoading)
        {
            xmlNode->getAttr("DefaultValue", m_bDefaultValue);
        }
        else
        {
            // saving
            xmlNode->setAttr("DefaultValue", m_bDefaultValue);
        }
        return retVal;
    }

    static bool BoolTrackVersionConverter(AZ::SerializeContext& serializeContext, AZ::SerializeContext::DataElementNode& rootElement)
    {
        if (rootElement.GetVersion() < 3)
        {
            rootElement.AddElement(serializeContext, "BaseClass1", azrtti_typeid<IAnimTrack>());
        }

        return true;
    }

    template<>
    inline void TAnimTrack<IBoolKey>::Reflect(AZ::ReflectContext* context)
    {
        if (auto serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<TAnimTrack<IBoolKey>, IAnimTrack>()
                ->Version(3, &BoolTrackVersionConverter)
                ->Field("Flags", &TAnimTrack<IBoolKey>::m_flags)
                ->Field("Range", &TAnimTrack<IBoolKey>::m_timeRange)
                ->Field("ParamType", &TAnimTrack<IBoolKey>::m_nParamType)
                ->Field("Keys", &TAnimTrack<IBoolKey>::m_keys)
                ->Field("Id", &TAnimTrack<IBoolKey>::m_id);
        }
    }

    void CBoolTrack::Reflect(AZ::ReflectContext* context)
    {
        TAnimTrack<IBoolKey>::Reflect(context);

        if (auto serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<CBoolTrack, TAnimTrack<IBoolKey>>()->Version(1)->Field("DefaultValue", &CBoolTrack::m_bDefaultValue);
        }
    }

} // namespace Maestro
