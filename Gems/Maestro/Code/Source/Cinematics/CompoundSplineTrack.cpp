/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/Math/Transform.h>

#include "CompoundSplineTrack.h"
#include "AnimSplineTrack.h"
#include "Maestro/Types/AnimParamType.h"
#include "Maestro/Types/AnimValueType.h"

namespace Maestro
{

    CCompoundSplineTrack::CCompoundSplineTrack(
        int nDims, AnimValueType inValueType, CAnimParamType subTrackParamTypes[MaxSubtracks], bool expanded)
        : m_refCount(0)
        , m_expanded(expanded)
    {
        AZ_Assert(nDims > 0 && nDims <= MaxSubtracks, "Spline Track dimension %i is out of range", nDims);
        m_node = nullptr;
        m_nDimensions = nDims;
        m_valueType = inValueType;

        m_nParamType = AnimParamType::Invalid;
        m_flags = 0;

        m_subTracks.resize(MaxSubtracks);
        for (int i = 0; i < m_nDimensions; i++)
        {
            m_subTracks[i].reset(aznew C2DSplineTrack());
            m_subTracks[i]->SetParameterType(subTrackParamTypes[i]);
        }

        m_subTrackNames.resize(MaxSubtracks);
        m_subTrackNames[0] = "X";
        m_subTrackNames[1] = "Y";
        m_subTrackNames[2] = "Z";
        m_subTrackNames[3] = "W";

        SetKeyValueRanges();
        RenameSubTracksIfNeeded();

#ifdef MOVIESYSTEM_SUPPORT_EDITING
        m_bCustomColorSet = false;
#endif
    }

    // Need default constructor for AZ Serialization
    CCompoundSplineTrack::CCompoundSplineTrack()
        : m_refCount(0)
        , m_nDimensions(0)
        , m_valueType(AnimValueType::Float)
#ifdef MOVIESYSTEM_SUPPORT_EDITING
        , m_bCustomColorSet(false)
#endif
        , m_expanded(false)
    {
    }

    void CCompoundSplineTrack::SetNode(IAnimNode* node)
    {
        if (!node)
        {
            AZ_Assert(false, "Expected valid node pointer.");
            return;
        }

        m_node = node;
        for (int i = 0; i < m_nDimensions; i++)
        {
            if (!m_subTracks[i])
            {
                AZ_Assert(false, "Expected valid sub-track(%d)", i);
                continue;
            }
            m_subTracks[i]->SetNode(node);
        }
    }
    void CCompoundSplineTrack::SetTimeRange(const Range& timeRange)
    {
        for (int i = 0; i < m_nDimensions; i++)
        {
            if (!m_subTracks[i])
            {
                AZ_Assert(false, "Expected valid sub-track(%d)", i);
                continue;
            }

            m_subTracks[i]->SetTimeRange(timeRange);
        }
    }

    Range CCompoundSplineTrack::GetTimeRange() const
    {
        return (m_subTracks[0]) ? m_subTracks[0]->GetTimeRange() : Range();
    }

    /// @deprecated Serialization for Sequence data in Component Entity Sequences now occurs through AZ::SerializeContext and the Sequence
    /// Component
    bool CCompoundSplineTrack::Serialize(XmlNodeRef& xmlNode, bool bLoading, bool bLoadEmptyTracks /*=true */)
    {
        if (!xmlNode)
        {
            AZ_Assert(false, "Expected valid node XML Node reference.");
            return false;
        }

#ifdef MOVIESYSTEM_SUPPORT_EDITING
        if (bLoading)
        {
            int flags = m_flags;
            xmlNode->getAttr("Flags", flags);
            SetFlags(flags);
            xmlNode->getAttr("HasCustomColor", m_bCustomColorSet);
            if (m_bCustomColorSet)
            {
                unsigned int abgr;
                xmlNode->getAttr("CustomColor", abgr);
                m_customColor = ColorB(abgr);
            }
            xmlNode->getAttr("Id", m_id);
        }
        else
        {
            int flags = GetFlags();
            xmlNode->setAttr("Flags", flags);
            xmlNode->setAttr("HasCustomColor", m_bCustomColorSet);
            if (m_bCustomColorSet)
            {
                xmlNode->setAttr("CustomColor", m_customColor.pack_abgr8888());
            }
            xmlNode->setAttr("Id", m_id);
        }
#endif

        bool result = true;
        for (int i = 0; i < m_nDimensions; i++)
        {
            if (!m_subTracks[i])
            {
                AZ_Assert(false, "Expected valid sub-track(%d)", i);
                result = false;
                continue;
            }

            XmlNodeRef subTrackNode;
            if (bLoading)
            {
                subTrackNode = xmlNode->getChild(i);
            }
            else
            {
                subTrackNode = xmlNode->newChild("NewSubTrack");
            }
            m_subTracks[i]->Serialize(subTrackNode, bLoading, bLoadEmptyTracks);
        }

        return result;
    }

    bool CCompoundSplineTrack::SerializeSelection(
        XmlNodeRef& xmlNode, bool bLoading, bool bCopySelected /*=false*/, float fTimeOffset /*=0*/)
    {
        if (!xmlNode)
        {
            AZ_Assert(false, "Expected valid node XML Node reference.");
            return false;
        }

        bool result = true;
        for (int i = 0; i < m_nDimensions; i++)
        {
            if (!m_subTracks[i])
            {
                AZ_Assert(false, "Expected valid sub-track(%d)", i);
                result = false;
                continue;
            }

            XmlNodeRef subTrackNode;
            if (bLoading)
            {
                subTrackNode = xmlNode->getChild(i);
            }
            else
            {
                subTrackNode = xmlNode->newChild("NewSubTrack");
            }
            result = result && m_subTracks[i]->SerializeSelection(subTrackNode, bLoading, bCopySelected, fTimeOffset);
        }
        return result;
    }

    void CCompoundSplineTrack::InitPostLoad([[maybe_unused]] IAnimSequence* sequence)
    {
        SetKeyValueRanges();
    }

    void CCompoundSplineTrack::GetValue(float time, float& value, bool applyMultiplier) const
    {
        value = 0.0f;
        for (int i = 0; i < 1 && i < m_nDimensions && m_subTracks[i]; i++)
        {
            if (!m_subTracks[i])
            {
                AZ_Assert(false, "Expected valid sub-track(%d)", i);
                continue;
            }

            m_subTracks[i]->GetValue(time, value, applyMultiplier);
        }
    }

    void CCompoundSplineTrack::GetValue(float time, AZ::Vector3& value, bool applyMultiplier) const
    {
        AZ_Assert(m_nDimensions == 3, "expect 3 sub-tracks, found %d.", m_nDimensions);
        for (int i = 0; i < m_nDimensions; i++)
        {
            if (!m_subTracks[i])
            {
                AZ_Assert(false, "Expected valid sub-track(%d)", i);
                continue;
            }

            float temp;
            m_subTracks[i]->GetValue(time, temp, applyMultiplier);
            value.SetElement(i, temp);
        }
    }

    void CCompoundSplineTrack::GetValue(float time, AZ::Vector4& value, bool applyMultiplier) const
    {
        AZ_Assert(m_nDimensions == 4, "Expected 4 sub-tracks, found %d.", m_nDimensions);
        for (int i = 0; i < m_nDimensions; i++)
        {
            if (!m_subTracks[i])
            {
                AZ_Assert(false, "Expected valid sub-track(%d)", i);
                continue;
            }

            float temp;
            m_subTracks[i]->GetValue(time, temp, applyMultiplier);
            value.SetElement(i, temp);
        }
    }

    void CCompoundSplineTrack::GetValue(float time, AZ::Quaternion& value) const
    {
        AZ_Assert(m_nDimensions == 3, "Expected 3 sub-tracks, found %d.", m_nDimensions);
        if (m_nDimensions == 3)
        {
            AZ::Vector3 angles; // Euler Angles XYZ
            GetValue(time, angles);
            // Use ZYX Euler (actually Tait-Bryan) rotation angles order instead of using CreateFromEulerDegreesXYZ(),
            // in order to provide "pitch, roll, yaw" editing in TrackView
            value = AZ::Quaternion::CreateFromEulerDegreesZYX(angles);
        }
        else
        {
            value = value.CreateIdentity();
        }
    }

    void CCompoundSplineTrack::SetValue(float time, const float& value, bool bDefault, bool applyMultiplier)
    {
        const Range timeRange(GetTimeRange());
        if (((timeRange.end - timeRange.start) > AZ::Constants::Tolerance) && (time < timeRange.start || time > timeRange.end))
        {
            AZ_WarningOnce("CompoundSplineTrack", false, "SetValue(%f, float): Time is out of range (%f .. %f) in track (%s), clamped.",
                time, timeRange.start, timeRange.end, (GetNode() ? GetNode()->GetName() : ""));
            AZStd::clamp(time, timeRange.start, timeRange.end);
        }

        for (int i = 0; i < m_nDimensions; i++)
        {
            if (!m_subTracks[i])
            {
                AZ_Assert(false, "Expected valid sub-track(%d)", i);
                return;
            }

            m_subTracks[i]->SetValue(time, value, bDefault, applyMultiplier);
        }
    }

    void CCompoundSplineTrack::SetValue(float time, const AZ::Vector3& value, bool bDefault, bool applyMultiplier)
    {
        const Range timeRange(GetTimeRange());
        if (((timeRange.end - timeRange.start) > AZ::Constants::Tolerance) && (time < timeRange.start || time > timeRange.end))
        {
            AZ_WarningOnce("CompoundSplineTrack", false, "SetValue(%f, Vector3): Time is out of range (%f .. %f) in track (%s), clamped.",
                time, timeRange.start, timeRange.end, (GetNode() ? GetNode()->GetName() : ""));
            AZStd::clamp(time, timeRange.start, timeRange.end);
        }

        for (int i = 0; i < m_nDimensions; i++)
        {
            if (!m_subTracks[i])
            {
                AZ_Assert(false, "Expected valid sub-track(%d)", i);
                return;
            }

            m_subTracks[i]->SetValue(time, value.GetElement(i), bDefault, applyMultiplier);
        }
    }

    void CCompoundSplineTrack::SetValue(float time, const AZ::Vector4& value, bool bDefault, bool applyMultiplier)
    {
        const Range timeRange(GetTimeRange());
        if (((timeRange.end - timeRange.start) > AZ::Constants::Tolerance) && (time < timeRange.start || time > timeRange.end))
        {
            AZ_WarningOnce("CompoundSplineTrack", false, "SetValue(%f, Vector4): Time is out of range (%f .. %f) in track (%s), clamped.",
                time, timeRange.start, timeRange.end, (GetNode() ? GetNode()->GetName() : ""));
            AZStd::clamp(time, timeRange.start, timeRange.end);
        }

        for (int i = 0; i < m_nDimensions; i++)
        {
            if (!m_subTracks[i])
            {
                AZ_Assert(false, "Expected valid sub-track(%d)", i);
                continue;
            }

            m_subTracks[i]->SetValue(time, value.GetElement(i), bDefault, applyMultiplier);
        }
    }

    void CCompoundSplineTrack::SetValue(float time, const AZ::Quaternion& value, bool bDefault)
    {
        const Range timeRange(GetTimeRange());
        if (((timeRange.end - timeRange.start) > AZ::Constants::Tolerance) && (time < timeRange.start || time > timeRange.end))
        {
            AZ_WarningOnce("CompoundSplineTrack", false, "SetValue(%f, Quat): Time is out of range (%f .. %f) in track (%s), clamped.",
                time, timeRange.start, timeRange.end, (GetNode() ? GetNode()->GetName() : ""));
            AZStd::clamp(time, timeRange.start, timeRange.end);
        }

        AZ_Assert(m_nDimensions == 3, "Expected 3 sub-tracks, found %d.", m_nDimensions);
        if (m_nDimensions == 3)
        {
            // Use ZYX Euler (actually Tait-Bryan) rotation angles order instead of using
            // GetEulerDegrees() (or GetEulerDegreesXYZ()), in order to provide "pitch, roll, yaw" editing in TrackView.
            AZ::Vector3 eulerAngle = value.GetEulerDegreesZYX();
            for (int i = 0; i < 3; i++)
            {
                if (!m_subTracks[i])
                {
                    AZ_Assert(false, "Expected valid sub-track(%d)", i);
                    continue;
                }

                float degree = eulerAngle.GetElement(i);
                if (false == bDefault)
                {
                    // Try to prefer the shortest path of rotation.
                    float degree0;
                    m_subTracks[i]->GetValue(time, degree0);
                    degree = PreferShortestRotPath(degree, degree0);
                }
                m_subTracks[i]->SetValue(time, degree, bDefault);
            }
        }
    }

    void CCompoundSplineTrack::OffsetKeyPosition(const AZ::Vector3& offset)
    {
        AZ_Assert(m_nDimensions == 3, "Expected 3 sub-tracks, found %d.", m_nDimensions);
        if (m_nDimensions == 3)
        {
            for (int i = 0; i < 3; i++)
            {
                if (!m_subTracks[i])
                {
                    AZ_Assert(false, "Expected valid sub-track(%d)", i);
                    continue;
                }

                IAnimTrack* pSubTrack = m_subTracks[i].get();
                // Iterate over all keys.
                for (int k = 0, num = pSubTrack->GetNumKeys(); k < num; k++)
                {
                    // Offset each key.
                    float time = pSubTrack->GetKeyTime(k);
                    float value = 0;
                    pSubTrack->GetValue(time, value);
                    value = value + offset.GetElement(i);
                    pSubTrack->SetValue(time, value);
                }
            }
        }
    }

    void CCompoundSplineTrack::UpdateKeyDataAfterParentChanged(const AZ::Transform& oldParentWorldTM, const AZ::Transform& newParentWorldTM)
    {
        // Only update the position tracks
        if (m_nParamType.GetType() != AnimParamType::Position || m_nDimensions != 3)
        {
            AZ_Assert(m_nParamType.GetType() != AnimParamType::Position, "Called for an invalid track (%s).", (GetNode() ? GetNode()->GetName() : ""));
            AZ_Assert(m_nDimensions == 3, "Expected 3 dimensions (position, rotation or scale), found %d.", m_nDimensions);
            return;
        }

        struct KeyValues
        {
            KeyValues(int i, float t, float v)
                : index(i)
                , time(t)
                , value(v) {}

            int index;
            float time;
            float value;
        };

        // Don't actually set the key data until we are done calculating all the new values.
        // In the case where not all 3 tracks have key data, data from other keys may be referenced
        // and used. So we don't want to muck with those other keys until we are done transforming all of
        // the key data.
        AZStd::vector<KeyValues> newKeyValues;

        // Collect all times that have key data on any track.
        for (int subTrackIndex = 0; subTrackIndex < 3; subTrackIndex++)
        {
            IAnimTrack* subTrack = m_subTracks[subTrackIndex].get();
            if (!subTrack)
            {
                AZ_Assert(false, "Expected valid sub-track(%d)", subTrackIndex);
                continue;
            }

            for (int k = 0, num = subTrack->GetNumKeys(); k < num; k++)
            {
                // If this key time is not already in the list, add it.
                float time = subTrack->GetKeyTime(k);

                // Create a 3 float vector with values from the 3 tracks.
                AZ::Vector3 vector;
                for (int i = 0; i < 3; i++)
                {
                    float value = 0;
                    m_subTracks[i]->GetValue(time, value);
                    vector.SetElement(i, value);
                }

                // Use the old parent world transform to get the current key data into world space.
                AZ::Vector3 worldPosition = oldParentWorldTM.GetTranslation() + vector;

                // Get the world location into local space relative to the new parent.
                vector = worldPosition - newParentWorldTM.GetTranslation();

                // Store the new key data in a list to be set to keys later.
                newKeyValues.push_back(KeyValues(subTrackIndex, time, vector.GetElement(subTrackIndex)));
            }
        }

        // Set key data for each time gathered from the keys.
        for (auto valuePair : newKeyValues)
        {
            const auto pSubTrack = m_subTracks[valuePair.index];
            if (!pSubTrack)
            {
                AZ_Assert(false, "Expected valid sub-track(%d)", valuePair.index);
                continue;
            }

            m_subTracks[valuePair.index]->SetValue(valuePair.time, valuePair.value);
        }
    }

    IAnimTrack* CCompoundSplineTrack::GetSubTrack(int nIndex) const
    {
        if (nIndex < 0 || nIndex >= m_nDimensions)
        {
            AZ_Assert(false, "Sub-track index (%d) is out of range (0 .. %d).", nIndex, m_nDimensions);
            return nullptr;
        }

        return m_subTracks[nIndex].get();
    }

    AZStd::string CCompoundSplineTrack::GetSubTrackName(int nIndex) const
    {
        if (nIndex < 0 || nIndex >= m_nDimensions)
        {
            AZ_Assert(false, "Sub-track index (%d) is out of range (0 .. %d).", nIndex, m_nDimensions);
            return AZStd::string();
        }
        if (!m_subTracks[nIndex])
        {
            AZ_Assert(false, "Expected valid sub-track(%d)", nIndex);
            return AZStd::string();
        }

        return m_subTrackNames[nIndex];
    }

    void CCompoundSplineTrack::SetSubTrackName(int nIndex, const char* name)
    {
        name = nullptr;
        if (nIndex < 0 || nIndex >= m_nDimensions || !name || !name[0])
        {
            AZ_Assert(nIndex >= 0 && nIndex < m_nDimensions, "Sub-track index (%d) is out of range (0 .. %d).", nIndex, m_nDimensions);
            AZ_Assert(name && name[0], "Subtrack name is null.");
            return;
        }
        if (!m_subTracks[nIndex])
        {
            AZ_Assert(false, "Expected valid sub-track(%d)", nIndex);
            return;
        }

        m_subTrackNames[nIndex] = name;
    }

    int CCompoundSplineTrack::GetNumKeys() const
    {
        int nKeys = 0;
        for (int i = 0; i < m_nDimensions; i++)
        {
            if (!m_subTracks[i])
            {
                AZ_Assert(false, "Expected valid sub-track(%d)", i);
                continue;
            }

            nKeys += m_subTracks[i]->GetNumKeys();
        }
        return nKeys;
    }

    bool CCompoundSplineTrack::HasKeys() const
    {
        for (int i = 0; i < m_nDimensions; i++)
        {
            if (!m_subTracks[i])
            {
                AZ_Assert(false, "Expected valid sub-track(%d)", i);
                return false;
            }

            if (m_subTracks[i]->GetNumKeys())
            {
                return true;
            }
        }
        return false;
    }

    float CCompoundSplineTrack::PreferShortestRotPath(float degree, float degree0) const
    {
        // Assumes the degree is in (-PI, PI).
        AZ_Assert(-180.01f < degree && degree < 180.01f, "degree %f is out of range", degree);
        float degree00 = degree0;
        degree0 = fmod_tpl(degree0, 360.0f);
        float n = (degree00 - degree0) / 360.0f;
        float degreeAlt;
        if (degree >= 0)
        {
            degreeAlt = degree - 360.0f;
        }
        else
        {
            degreeAlt = degree + 360.0f;
        }
        if (fabs(degreeAlt - degree0) < fabs(degree - degree0))
        {
            return degreeAlt + n * 360.0f;
        }
        else
        {
            return degree + n * 360.0f;
        }
    }

    int CCompoundSplineTrack::GetSubTrackIndex(int& keyIndex) const
    {
        if (keyIndex < 0 || keyIndex >= GetNumKeys())
        {
            AZ_Assert(false, "Key index (%d) is out of range (0 .. %d).", keyIndex, GetNumKeys());
            return -1;
        }

        int count = 0;
        for (int i = 0; i < m_nDimensions; i++)
        {
            if (!m_subTracks[i])
            {
                AZ_Assert(false, "Expected valid sub-track(%d)", i);
                return -1;
            }

            if (keyIndex < count + m_subTracks[i]->GetNumKeys())
            {
                keyIndex = keyIndex - count;
                return i;
            }
            count += m_subTracks[i]->GetNumKeys();
        }
        return -1;
    }

    void CCompoundSplineTrack::RemoveKey(int keyIndex)
    {
        if (keyIndex < 0 || keyIndex >= GetNumKeys())
        {
            AZ_Assert(false, "Key index (%d) is out of range (0 .. %d).", keyIndex, GetNumKeys());
            return;
        }

        const auto firstTrackIdx = GetSubTrackIndex(keyIndex); // Key index is now adjusted to the selected track.
        if (firstTrackIdx < 0)
        {
            AZ_Error("CompoundSplineTrack", false, "RemoveKey(%d): No keys with this index found in track (%s).",
                keyIndex, (GetNode() ? GetNode()->GetName() : ""));
            return;
        }

        const auto time = m_subTracks[firstTrackIdx]->GetKeyTime(keyIndex);
        for (int i = 0; i < this->GetSubTrackCount(); ++i)
        {
            if (!m_subTracks[i])
            {
                AZ_Assert(false, "Expected valid sub-track(%d)", i);
                continue;
            }
            
            if (const int subKeyIdx = m_subTracks[i]->FindKey(time) >= 0)
            {
                m_subTracks[i]->RemoveKey(subKeyIdx);
            }
        }
    }

    int CCompoundSplineTrack::CreateKey(float time)
    {
        const Range timeRange(GetTimeRange());
        if (((timeRange.end - timeRange.start) > AZ::Constants::Tolerance) && (time < timeRange.start || time > timeRange.end))
        {
            AZ_WarningOnce("CompoundSplineTrack", false, "CreateKey(%f): Time is out of range (%f .. %f) in track (%s), clamped.",
                time, timeRange.start, timeRange.end, (GetNode() ? GetNode()->GetName() : ""));
            AZStd::clamp(time, timeRange.start, timeRange.end);
        }

        const auto existingKeyIndex = FindKey(time);
        if (existingKeyIndex >= 0)
        {
            AZ_Error("CompoundSplineTrack", false, "CreateKey(%f): A key (%d) with this time exists in track (%s).",
                time, existingKeyIndex, (GetNode() ? GetNode()->GetName() : ""));
            return -1;
        }

        for (int i = 0; i < m_nDimensions; ++i)
        {
            if (!m_subTracks[i])
            {
                AZ_Assert(false, "Expected valid sub-track(%d)", i);
                continue;
            }

            m_subTracks[i]->CreateKey(time);
        }

        return FindKey(time);
    }

    void CCompoundSplineTrack::GetKeyInfo(int keyIndex, const char*& description, float& duration) const
    {
        duration = 0;
        description = 0;

        if (keyIndex < 0 || keyIndex >= GetNumKeys())
        {
            AZ_Assert(false, "Key index (%d) is out of range (0 .. %d).", keyIndex, GetNumKeys());
            return;
        }

        static char str[64];
        const char* subDesc = nullptr;
        float time = GetKeyTime(keyIndex);

        int m = 0;
        /// Using the time obtained, combine descriptions from keys of the same time
        /// in sub-tracks if any into one compound description.
        str[0] = 0;
        // A head case
        const auto numKeysInFirstSubTrack = m_subTracks[0]->GetNumKeys();
        for (m = 0; m < numKeysInFirstSubTrack; ++m)
        {
            if (AZStd::abs(m_subTracks[0]->GetKeyTime(m) - time) < s_MinTimePrecision)
            {
                float dummy;
                m_subTracks[0]->GetKeyInfo(m, subDesc, dummy);
                azstrcat(str, AZ_ARRAY_SIZE(str), subDesc);
                break;
            }
        }
        if (m == numKeysInFirstSubTrack)
        {
            azstrcat(str, AZ_ARRAY_SIZE(str), m_subTrackNames[0].c_str());
        }

        // Tail cases
        for (int i = 1; i < GetSubTrackCount(); ++i)
        {
            if (!m_subTracks[i])
            {
                AZ_Assert(false, "Expected valid sub-track(%d)", i);
                continue;
            }

            azstrcat(str, AZ_ARRAY_SIZE(str), ",");
            for (m = 0; m < m_subTracks[i]->GetNumKeys(); ++m)
            {
                if (m_subTracks[i]->GetKeyTime(m) == time)
                {
                    float dummy;
                    m_subTracks[i]->GetKeyInfo(m, subDesc, dummy);
                    azstrcat(str, AZ_ARRAY_SIZE(str), subDesc);
                    break;
                }
            }
            if (m == m_subTracks[i]->GetNumKeys())
            {
                azstrcat(str, AZ_ARRAY_SIZE(str), m_subTrackNames[i].c_str());
            }
        }
    }

    float CCompoundSplineTrack::GetKeyTime(int keyIndex) const
    {
        if (keyIndex < 0 || keyIndex >= GetNumKeys())
        {
            AZ_Assert(false, "Key index (%d) is out of range (0 .. %d).", keyIndex, GetNumKeys());
            return -1.0f;
        }

        const auto trackIdx = GetSubTrackIndex(keyIndex); // Key index is now adjusted to the selected track.
        if (trackIdx < 0)
        {
            AZ_Error("CompoundSplineTrack", false, "GetKeyTime(%d): No keys with this index found in track (%s).",
                keyIndex, (GetNode() ? GetNode()->GetName() : ""));
            return -1.0f;
        }

        return m_subTracks[trackIdx]->GetKeyTime(keyIndex);
    }

    int CCompoundSplineTrack::FindKey(float time) const
    {
        int keysCount = 0;
        for (int i = 0; i < GetSubTrackCount(); ++i)
        {
            const auto numKeysInTrack = m_subTracks[i]->GetNumKeys();
            for (int m = 0; m < numKeysInTrack; ++m)
            {
                if (!m_subTracks[i])
                {
                    AZ_Assert(false, "Expected valid sub-track(%d)", i);
                    continue;
                }

                if (AZStd::abs(m_subTracks[i]->GetKeyTime(m) - time) < AZ::Constants::Tolerance)
                {
                    return keysCount + m;
                }
            }
        }
        return -1;
    }

    void CCompoundSplineTrack::SetKeyTime(int keyIndex, float time)
    {
        if (keyIndex < 0 || keyIndex >= GetNumKeys())
        {
            AZ_Assert(false, "Key index (%d) is out of range (0 .. %d).", keyIndex, GetNumKeys());
            return;
        }

        const Range timeRange(GetTimeRange());
        if (((timeRange.end - timeRange.start) > AZ::Constants::Tolerance) && (time < timeRange.start || time > timeRange.end))
        {
            AZ_WarningOnce("CompoundSplineTrack", false, "SetKeyTime(%d, %f): Time is out of range (%f .. %f) in track (%s), clamped.",
                keyIndex, time, timeRange.start, timeRange.end, (GetNode() ? GetNode()->GetName() : ""));
            AZStd::clamp(time, timeRange.start, timeRange.end);
        }

        const int existingKeyIndex = FindKey(time);
        if (existingKeyIndex >= 0)
        {
            AZ_Error("CompoundSplineTrack", existingKeyIndex == keyIndex, "SetKeyTime(%d, %f): A key (%d) with this time exists in track (%s).",
                keyIndex, time, existingKeyIndex, (GetNode() ? GetNode()->GetName() : ""));
            return;
        }

        const auto trackIdx = GetSubTrackIndex(keyIndex); // Key index is now adjusted to the selected track.
        if (trackIdx < 0)
        {
            AZ_Error("CompoundSplineTrack", false, "SetKeyTime(%d, %f): No keys with this key index found in track (%s).",
                keyIndex, time, (GetNode() ? GetNode()->GetName() : ""));
            return;
        }

        // Change keys time in sub-tracks
        const auto targetKeyTime = m_subTracks[trackIdx]->GetKeyTime(keyIndex);
        for (int i = 0; i < GetSubTrackCount(); ++i)
        {
            if (!m_subTracks[i])
            {
                AZ_Assert(false, "Expected valid sub-track(%d)", i);
                continue;
            }

            const int existingSubTrackKeyIndex = m_subTracks[i]->FindKey(targetKeyTime);
            if (existingSubTrackKeyIndex >= 0)
            {
                m_subTracks[i]->SetKeyTime(existingSubTrackKeyIndex, time);
            }
        }
    }

    bool CCompoundSplineTrack::IsKeySelected(int keyIndex) const
    {
        if (keyIndex < 0 || keyIndex >= GetNumKeys())
        {
            AZ_Assert(false, "Key index (%d) is out of range (0 .. %d).", keyIndex, GetNumKeys());
            return false;
        }

        // Initial key index is belonging to the set of all keys in all sub-tracks
        int trackIdx = GetSubTrackIndex(keyIndex); // Key index is now adjusted to the selected track.
        if (trackIdx < 0)
        {
            AZ_Warning("CompoundSplineTrack", false, "IsKeySelected(%d): No keys with this key index found in track (%s).",
                keyIndex, (GetNode() ? GetNode()->GetName() : ""));
            return false;
        }

        // The key at m_subTracks[firstIdx][key] maybe selected or not.
        // A "logical key" for a compound track is regarded as selected when all keys at the same timeline are selected
        const auto firstKeyTime = m_subTracks[trackIdx]->GetKeyTime(keyIndex);

        for (int subTrackIdx = 0; subTrackIdx < GetSubTrackCount(); ++subTrackIdx)
        {
            const auto pSubTrack = m_subTracks[subTrackIdx];
            if (!pSubTrack)
            {
                AZ_Assert(false, "Expected valid sub-track(%d)", subTrackIdx);
                continue;
            }

            for (int keyIdx = 0; keyIdx < pSubTrack->GetNumKeys(); ++keyIdx)
            {
                const auto subKeyTime = pSubTrack->GetKeyTime(keyIdx);
                if ((AZStd::abs(firstKeyTime - subKeyTime) < AZ::Constants::Tolerance) && !pSubTrack->IsKeySelected(keyIdx))
                {
                    return false;
                }
            }
        }

        return true;
    }

    void CCompoundSplineTrack::SelectKey(int keyIndex, bool select)
    {
        if (keyIndex < 0 || keyIndex >= GetNumKeys())
        {
            AZ_Assert(false, "Key index (%d) is out of  range (0..%d).", keyIndex, GetNumKeys());
            return;
        }

        // Initial key index is belonging to the set of all keys in all sub-tracks
        int trackIdx = GetSubTrackIndex(keyIndex); // Key index is now adjusted to the selected track.
        if (trackIdx < 0)
        {
            AZ_Warning("CompoundSplineTrack", false, "SelectKey(%d): No keys with this key index found in track (%s).",
                keyIndex, (GetNode() ? GetNode()->GetName() : ""));
            return;
        }

        float keyTime = m_subTracks[trackIdx]->GetKeyTime(keyIndex);
        // In the case of compound tracks, animators want to
        // select all keys of the same time in the sub-tracks together.
        for (int i = 0; i < m_nDimensions; ++i)
        {
            if (!m_subTracks[i])
            {
                AZ_Assert(false, "Expected valid sub-track(%d)", i);
                continue;
            }

            for (int k = 0; k < m_subTracks[i]->GetNumKeys(); ++k)
            {
                if (fabs(m_subTracks[i]->GetKeyTime(k) - keyTime) < AZ::Constants::Tolerance)
                {
                    m_subTracks[i]->SelectKey(k, select);
                    break;
                }
            }
        }
    }

    int CCompoundSplineTrack::NextKeyByTime(int keyIndex) const
    {
        if (keyIndex < 0 || keyIndex >= GetNumKeys())
        {
            AZ_Assert(false, "Key index (%d) is out of  range (0 .. %d).", keyIndex, GetNumKeys());
            return -1;
        }

        float time = GetKeyTime(keyIndex);
        int count = 0;
        int result = -1;
        float timeNext = FLT_MAX;
        for (int i = 0; i < GetSubTrackCount(); ++i)
        {
            if (!m_subTracks[i])
            {
                AZ_Assert(false, "Expected valid sub-track(%d)", i);
                continue;
            }

            for (int k = 0; k < m_subTracks[i]->GetNumKeys(); ++k)
            {
                float t = m_subTracks[i]->GetKeyTime(k);
                if (t > time)
                {
                    if (t < timeNext)
                    {
                        timeNext = t;
                        result = count + k;
                    }
                    break;
                }
            }
            count += m_subTracks[i]->GetNumKeys();
        }
        return result;
    }

    void CCompoundSplineTrack::RenameSubTracksIfNeeded()
    {
        switch (m_valueType)
        {
        case AnimValueType::RGB:
            {
                if (m_nDimensions != 3)
                {
                    AZ_Assert(false, "Invalid dimensions %d for RGB track", m_nDimensions);
                    return;
                }
                if (!(m_subTracks[0] && m_subTracks[1] && m_subTracks[2]))
                {
                    AZ_Assert(m_subTracks[0], "Expected valid sub-track(0)");
                    AZ_Assert(m_subTracks[1], "Expected valid sub-track(1)");
                    AZ_Assert(m_subTracks[2], "Expected valid sub-track(2)");
                    return;
                }

                m_subTrackNames[0] = "Red";
                m_subTrackNames[1] = "Green";
                m_subTrackNames[2] = "Blue";
            }
            return;
        case AnimValueType::Quat:
            {
                if (m_nDimensions != 3)
                {
                    AZ_Assert(false, "Invalid dimensions %d for Quaternion track", m_nDimensions);
                    return;
                }
                if (!(m_subTracks[0] && m_subTracks[1] && m_subTracks[2]))
                {
                    AZ_Assert(m_subTracks[0], "Expected valid sub-track(0)");
                    AZ_Assert(m_subTracks[1], "Expected valid sub-track(1)");
                    AZ_Assert(m_subTracks[2], "Expected valid sub-track(2)");
                    return;
                }

                m_subTrackNames[0] = "Pitch";
                m_subTrackNames[1] = "Roll";
                m_subTrackNames[2] = "Yaw";
            }
            return;
        default:
            // Do nothing, a specific factory method should handle this.
            return;
        }
    }

    void CCompoundSplineTrack::SetKeyValueRanges()
    {
        switch (m_valueType)
        {
        case AnimValueType::RGB:
            {
                SetKeyValueRange(0.0f, 255.f);
                return;
            }
        case AnimValueType::Quat:
            {
                if (m_nDimensions != 3)
                {
                    AZ_Assert(false, "Invalid dimensions %d for Quaternion track", m_nDimensions);
                    return;
                }
                if (!(m_subTracks[0] && m_subTracks[1] && m_subTracks[2]))
                {
                    AZ_Assert(m_subTracks[0], "Expected valid sub-track(0)");
                    AZ_Assert(m_subTracks[1], "Expected valid sub-track(1)");
                    AZ_Assert(m_subTracks[2], "Expected valid sub-track(2)");
                    return;
                }

                m_subTracks[0]->SetKeyValueRange(-90.0f, 90.0f);
                m_subTracks[1]->SetKeyValueRange(-180.0f, 180.0f);
                m_subTracks[2]->SetKeyValueRange(-180.0f, 180.0f);
                return;
            }
        case AnimValueType::Vector:
            {
                for (int i = 0; i < m_nDimensions; ++i)
                {
                    if (!m_subTracks[i])
                    {
                        AZ_Assert(false, "Expected valid sub-track(%d)", i);
                        continue;
                    }

                    switch (m_subTracks[i]->GetParameterType().GetType())
                    {
                    case AnimParamType::PositionX:
                    case AnimParamType::PositionY:
                    case AnimParamType::PositionZ:
                        {
                            m_subTracks[i]->SetKeyValueRange(-100.0f, 100.0f);
                            break;
                        }
                    case AnimParamType::ScaleX:
                    case AnimParamType::ScaleY:
                    case AnimParamType::ScaleZ:
                        {
                            m_subTracks[i]->SetKeyValueRange(0.01f, 100.0f);
                            break;
                        }
                    case AnimParamType::DepthOfField: // Do nothing, DepthOfField handled in factory methods.
                    default: // Do nothing, rotations were handled above, others should be handled in factory methods or added here.
                        break;
                    }
                }
                return;
            }
        case AnimValueType::Float:
        case AnimValueType::DiscreteFloat:
            {
                for (int i = 0; i < GetSubTrackCount(); i++)
                {
                    if (!m_subTracks[i])
                    {
                        AZ_Assert(false, "Expected valid sub-track(%d)", i);
                        continue;
                    }

                    float fMinKeyValue = 0;
                    float fMaxKeyValue = 0;
                    m_subTracks[i]->GetKeyValueRange(fMinKeyValue, fMaxKeyValue);
                    if (fMaxKeyValue - fMinKeyValue < AZ::Constants::Tolerance)
                    {
                        m_subTracks[i]->SetKeyValueRange(-1.0f, 1.0f);
                    }
                }
                return;
            }
        case AnimValueType::Vector4:
            {
                return; // Do nothing, a specific factory method should handle this.
            }
        default:
            {
                AZ_Error("CompoundSplineTrack", false, "ResetKeyValueRanges(): Unexpected ValueType %d.", static_cast<int>(m_valueType));
                return;
            }
        }
    }

    void CCompoundSplineTrack::SetExpanded(bool expanded)
    {
        m_expanded = expanded;
    }

    bool CCompoundSplineTrack::GetExpanded() const
    {
        return m_expanded;
    }

    unsigned int CCompoundSplineTrack::GetId() const
    {
        return m_id;
    }

    void CCompoundSplineTrack::SetId(unsigned int id)
    {
        m_id = id;
    }

    static bool CompoundSplineTrackVersionConverter(
        AZ::SerializeContext& serializeContext, AZ::SerializeContext::DataElementNode& rootElement)
    {
        if (rootElement.GetVersion() < 4)
        {
            rootElement.AddElement(serializeContext, "BaseClass1", azrtti_typeid<IAnimTrack>());
        }

        return true;
    }

    void CCompoundSplineTrack::Reflect(AZ::ReflectContext* context)
    {
        if (auto serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<CCompoundSplineTrack, IAnimTrack>()
                ->Version(4, &CompoundSplineTrackVersionConverter)
                ->Field("Flags", &CCompoundSplineTrack::m_flags)
                ->Field("ParamType", &CCompoundSplineTrack::m_nParamType)
                ->Field("NumSubTracks", &CCompoundSplineTrack::m_nDimensions)
                ->Field("SubTracks", &CCompoundSplineTrack::m_subTracks)
                ->Field("SubTrackNames", &CCompoundSplineTrack::m_subTrackNames)
                ->Field("ValueType", &CCompoundSplineTrack::m_valueType)
                ->Field("Expanded", &CCompoundSplineTrack::m_expanded)
                ->Field("Id", &CCompoundSplineTrack::m_id);
        }
    }

} // namespace Maestro
