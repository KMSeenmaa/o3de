/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */


#pragma once

#include <IMovieSystem.h>
#include "AnimTrack.h"

namespace Maestro
{

    /** Boolean track, every key on this track negates boolean value.
     */
    class CBoolTrack : public TAnimTrack<IBoolKey>
    {
    public:
        AZ_CLASS_ALLOCATOR(CBoolTrack, AZ::SystemAllocator);
        AZ_RTTI(CBoolTrack, "{A98E28CB-DE42-47A3-8E4B-6B43A5F3D8B2}", IAnimTrack);

        CBoolTrack();

        AnimValueType GetValueType() const override;

        void GetValue(float time, bool& value) const override;
        void SetValue(float time, const bool& value, bool bDefault = false) override;

        void SerializeKey([[maybe_unused]] IBoolKey& key, [[maybe_unused]] XmlNodeRef& keyNode, [[maybe_unused]] bool bLoading) override {}
        void GetKeyInfo(int keyIndex, const char*& description, float& duration) const override;

        void SetDefaultValue(const bool bDefaultValue);

        bool Serialize(XmlNodeRef& xmlNode, bool bLoading, bool bLoadEmptyTracks = true) override;

        static void Reflect(AZ::ReflectContext* context);

    private:
        bool m_bDefaultValue;
    };

} // namespace Maestro
