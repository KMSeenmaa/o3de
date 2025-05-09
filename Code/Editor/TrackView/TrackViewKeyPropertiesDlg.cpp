/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */


#include "EditorDefs.h"

#include "KeyUIControls.h"
#include "TrackViewKeyPropertiesDlg.h"

// Qt
#include <QMessageBox>

// AzCore
#include <AzCore/std/sort.h>

// CryCommon
#include <CryCommon/Maestro/Types/AnimValueType.h>

// Editor
#include "Controls/ReflectedPropertyControl/ReflectedPropertyCtrl.h"

AZ_PUSH_DISABLE_DLL_EXPORT_MEMBER_WARNING
#include <TrackView/ui_TrackViewTrackPropsDlg.h>
AZ_POP_DISABLE_DLL_EXPORT_MEMBER_WARNING


void CTrackViewKeyUIControls::OnInternalVariableChange(IVariable* var)
{
    CTrackViewSequence* sequence = GetIEditor()->GetAnimation()->GetSequence();
    AZ_Assert(sequence, "Expected valid sequence.");
    if (sequence)
    {
        CTrackViewKeyBundle keys = sequence->GetSelectedKeys();
        OnUIChange(var, keys);
    }
}

CTrackViewKeyPropertiesDlg::CTrackViewKeyPropertiesDlg(QWidget* hParentWnd)
    : QWidget(hParentWnd)
    , m_pLastTrackSelected(nullptr)
    , m_sequence(nullptr)
{
    QVBoxLayout* l = new QVBoxLayout();
    l->setMargin(0);
    m_wndTrackProps = new CTrackViewTrackPropsDlg(this);
    l->addWidget(m_wndTrackProps);
    m_wndProps = new ReflectedPropertyControl(this);
    m_wndProps->Setup(true, 120);
    m_wndProps->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    l->addWidget(m_wndProps);

    m_wndProps->SetStoreUndoByItems(false);

    setLayout(l);

    m_pVarBlock = new CVarBlock;

    // Add key UI classes
    // Compound tracks
    m_keyControls.push_back(new CQuatKeyUIControls());
    m_keyControls.push_back(new CRgbKeyUIControls());
    m_keyControls.push_back(new CVectorKeyUIControls());
    m_keyControls.push_back(new CVector4KeyUIControls());
    // Simple tracks
    m_keyControls.push_back(new C2DBezierKeyUIControls());
    m_keyControls.push_back(new CAssetBlendKeyUIControls());
    m_keyControls.push_back(new CCaptureKeyUIControls());
    m_keyControls.push_back(new CCommentKeyUIControls());
    m_keyControls.push_back(new CConsoleKeyUIControls());
    m_keyControls.push_back(new CEventKeyUIControls());
    m_keyControls.push_back(new CGotoKeyUIControls());
    m_keyControls.push_back(new CScreenFaderKeyUIControls());
    m_keyControls.push_back(new CSelectKeyUIControls());
    m_keyControls.push_back(new CSequenceKeyUIControls());
    m_keyControls.push_back(new CSoundKeyUIControls());
    m_keyControls.push_back(new CStringKeyUIControls());
    m_keyControls.push_back(new CTimeRangeKeyUIControls());
    m_keyControls.push_back(new CTrackEventKeyUIControls());

    // Sort key controls by descending priority
    AZStd::stable_sort(m_keyControls.begin(), m_keyControls.end(),
        [](const _smart_ptr<CTrackViewKeyUIControls>& a, const _smart_ptr<CTrackViewKeyUIControls>& b)
        {
            return a->GetPriority() > b->GetPriority();
        }
        );

    CreateAllVars();
}

void CTrackViewKeyPropertiesDlg::OnVarChange(IVariable* pVar)
{
    // If it was a motion that just changed, we need to rebuild the controls
    // so the min/max on the sliders update correctly.
    if (m_sequence && pVar->GetDataType() == IVariable::DT_MOTION)
    {
        OnKeySelectionChanged(m_sequence);
    }
}

void CTrackViewKeyPropertiesDlg::CreateAllVars()
{
    for (const auto& keyControl : m_keyControls)
    {
        keyControl->SetKeyPropertiesDlg(this);
        keyControl->OnCreateVars();
    }
}

void CTrackViewKeyPropertiesDlg::PopulateVariables()
{
    // Must first clear any selection in properties window.
    m_wndProps->RemoveAllItems();
    m_wndProps->AddVarBlock(m_pVarBlock);

    m_wndProps->SetUpdateCallback([this](IVariable* var) { OnVarChange(var); });
    //m_wndProps->m_props.ExpandAll();


    ReloadValues();
}

void CTrackViewKeyPropertiesDlg::PopulateVariables(ReflectedPropertyControl* propCtrl)
{
    propCtrl->RemoveAllItems();
    propCtrl->AddVarBlock(m_pVarBlock);

    propCtrl->ReloadValues();
}

void CTrackViewKeyPropertiesDlg::OnKeysChanged(CTrackViewSequence* pSequence)
{
    const CTrackViewKeyBundle& selectedKeys = pSequence->GetSelectedKeys();

    const auto numSelectedKeys = selectedKeys.GetKeyCount();
    if (numSelectedKeys > 0 && selectedKeys.AreAllKeysOfSameType())
    {
        auto pTrack = selectedKeys.GetKey(0).GetTrack();
        if (!pTrack)
        {
            return;
        }

        const bool areSubTrackKeysSelected = (numSelectedKeys > 1) && pTrack->IsSubTrack();
        if (areSubTrackKeysSelected)
        {
            if (const auto pParentNode = pTrack->GetParentNode())
            {
                if (pParentNode->GetNodeType() == ETrackViewNodeType::eTVNT_Track)
                {
                    pTrack = static_cast<CTrackViewTrack*>(pParentNode);
                }
            }
        }

        const CAnimParamType paramType = pTrack->GetParameterType();
        const EAnimCurveType trackType = pTrack->GetCurveType();
        const AnimValueType valueType = pTrack->GetValueType();

        for (const auto& keyControl : m_keyControls)
        {
            if (keyControl->SupportTrackType(paramType, trackType, valueType))
            {
                keyControl->OnKeySelectionChange(selectedKeys);
                break;
            }
        }
    }
}

void CTrackViewKeyPropertiesDlg::OnKeySelectionChanged(CTrackViewSequence* sequence)
{
    auto cleanup = [this]()
    {
        m_wndProps->ClearSelection();
        m_pVarBlock->DeleteAllVariables();
        m_wndProps->setEnabled(false);
        m_wndTrackProps->setEnabled(false);
        m_pLastTrackSelected = nullptr;
    };

    m_sequence = sequence;
    if (!sequence)
    {
        cleanup();
        return;
    }

    const CTrackViewKeyBundle& selectedKeys = sequence->GetSelectedKeys();
    const auto numSelectedKeys = selectedKeys.GetKeyCount();
    if (numSelectedKeys < 1 || !selectedKeys.AreAllKeysOfSameType())
    {
        cleanup();
        return;
    }

    m_wndTrackProps->OnKeySelectionChange(selectedKeys);

    const auto pFirstTrack = selectedKeys.GetKey(0).GetTrack();
    if (!pFirstTrack)
    {
        cleanup();
        return;
    }

    auto pTrack = pFirstTrack;
    // Check if a Compound track is selected with keys selected in sub-tracks?
    if ((numSelectedKeys > 1) && pFirstTrack->IsSubTrack())
    {
        if (const auto pParentNode = pFirstTrack->GetParentNode())
        {
            if (pParentNode->GetNodeType() == ETrackViewNodeType::eTVNT_Track)
            {
                pTrack = static_cast<CTrackViewTrack*>(pParentNode);
            }
        }
    }

    const bool bSelectChangedInSameTrack = m_pLastTrackSelected && pTrack == m_pLastTrackSelected;

    // Every Key in an Asset Blend track can have different min/max values on the float sliders
    // because it's based on the duration of the motion that is set. So don't try to
    // reuse the controls when the selection changes, otherwise the tooltips may be wrong.
    const bool reuseControls = bSelectChangedInSameTrack && m_pLastTrackSelected && (m_pLastTrackSelected->GetValueType() != AnimValueType::AssetBlend);

    m_pLastTrackSelected = pTrack;

    if (reuseControls)
    {
        m_wndProps->ClearSelection();
    }
    else
    {
        m_pVarBlock->DeleteAllVariables();
    }

    m_wndProps->setEnabled(false);
    m_wndTrackProps->setEnabled(false);
    if (selectedKeys.GetKeyCount() > 0 && selectedKeys.AreAllKeysOfSameType())
    {
        if (!reuseControls)
        {
            const CAnimParamType paramType = pTrack->GetParameterType();
            const EAnimCurveType trackType = pTrack->GetCurveType();
            const AnimValueType  valueType = pTrack->GetValueType();

            for (const auto& keyControl : m_keyControls)
            {
                if (keyControl->SupportTrackType(paramType, trackType, valueType))
                {
                    AddVars(keyControl);
                    break;
                }
            }
        }

        m_wndProps->setEnabled(true);
        m_wndTrackProps->setEnabled(true);
    }
    else
    {
        m_wndProps->setEnabled(false);
        m_wndTrackProps->setEnabled(false);
    }

    if (reuseControls)
    {
        ReloadValues();
    }
    else
    {
        PopulateVariables();
    }
    OnKeysChanged(sequence);
}

void CTrackViewKeyPropertiesDlg::AddVars(CTrackViewKeyUIControls* pUI)
{
    CVarBlock* pVB = pUI->GetVarBlock();
    for (int i = 0, num = pVB->GetNumVariables(); i < num; i++)
    {
        IVariable* pVar = pVB->GetVariable(i);
        m_pVarBlock->AddVariable(pVar);
    }
}

void CTrackViewKeyPropertiesDlg::ReloadValues()
{
    m_wndProps->ReloadValues();
}

void CTrackViewKeyPropertiesDlg::OnSequenceChanged(CTrackViewSequence* sequence)
{
    OnKeySelectionChanged(sequence);
    m_wndTrackProps->OnSequenceChanged();
}

CTrackViewTrackPropsDlg::CTrackViewTrackPropsDlg(QWidget* parent /* = 0 */)
    : QWidget(parent)
    , ui(new Ui::CTrackViewTrackPropsDlg)
{
    ui->setupUi(this);

    // Use editingFinished and a custom signal stepByFinished (and not valueChanged)
    // so the time will be updated when the user finishes editing the time field (hits enter)
    // or if the arrow keys (or mouse click on the arrow buttons) in the spinner box are
    // pressed. Don't just use valueChanged because we don't want intermediate values
    // like 1 as the user is typing 10 to register as updates to the key values. Keys
    // are identified by time, so the keys jumping around like that can stomp existing keys
    // that happen to live at the intermediate values.
    connect(ui->TIME, SIGNAL(editingFinished()), this, SLOT(OnUpdateTime()));
    connect(ui->TIME, SIGNAL(stepByFinished()), this, SLOT(OnUpdateTime()));
}

CTrackViewTrackPropsDlg::~CTrackViewTrackPropsDlg()
{
}

void CTrackViewTrackPropsDlg::OnSequenceChanged()
{
    CTrackViewSequence* pSequence = GetIEditor()->GetAnimation()->GetSequence();

    if (pSequence)
    {
        Range range = pSequence->GetTimeRange();
        ui->TIME->setRange(range.start, range.end);
    }
}

bool CTrackViewTrackPropsDlg::OnKeySelectionChange(const CTrackViewKeyBundle& selectedKeys)
{
    auto keyHandle = selectedKeys.GetSingleSelectedKey();

    if (keyHandle.IsValid())
    {
        m_selectedKeys = selectedKeys;

        // Block the callback, the values is already set in m_keyHandle.GetTime(), no need to
        // reset it and create an undo even like the user was setting it via the UI.
        ui->TIME->blockSignals(true);
        ui->TIME->setValue(keyHandle.GetTime());
        ui->TIME->blockSignals(false);

        ui->PREVNEXT->setText(QString::number(keyHandle.GetIndex() + 1));
        ui->PREVNEXT->setEnabled(true);
        ui->TIME->setEnabled(true);
    }
    else
    {
        m_selectedKeys = CTrackViewKeyBundle();

        ui->PREVNEXT->setText(QString());
        ui->PREVNEXT->setEnabled(false);
        ui->TIME->setEnabled(false);
    }

    return keyHandle.IsValid();
}

void CTrackViewTrackPropsDlg::OnUpdateTime()
{
    if (m_selectedKeys.GetKeyCount() < 1)
    {
        return;
    }

    const bool isDuringUndo = AzToolsFramework::UndoRedoOperationInProgress();
    AZStd::unique_ptr<AzToolsFramework::ScopedUndoBatch> undoBatch;
    if (!isDuringUndo)
    {
        undoBatch = AZStd::make_unique<AzToolsFramework::ScopedUndoBatch>("Change Keys Time");
    }

    const float time = (float)ui->TIME->value();
    bool allowDeletingKeys = false;

    for (unsigned int i = 0; i < m_selectedKeys.GetKeyCount(); ++i)
    {
        auto keyHandle = m_selectedKeys.GetKey(i);
        auto track = keyHandle.GetTrack();
        if (!keyHandle.IsValid() || !track)
        {
            AZ_Assert(false, "Invalid key handle for a selected key %u.", i);
            continue;
        }

        if (AZ::IsClose(keyHandle.GetTime(), time, AZ::Constants::FloatEpsilon))
        {
            continue; // nothing to do
        }

        // Check if the sequence is legacy
        const auto sequence = track->GetSequence();
        if (!sequence)
        {
            AZ_Assert(false, "Each track should have a sequence, invalid key handle for a selected key %u.", i);
            continue;
        }
        const auto sequenceId = sequence->GetSequenceComponentEntityId();

        if (!isDuringUndo)
        {
            CTrackViewKeyHandle existingKey = track->GetKeyByTime(time);

            // If there is an existing key at this time, remove it so the
            // new key at this time is the only one here. Make sure it's actually a different
            // key, because time can "change" but then be quantized (or snapped) to the same time by track->GetKeyByTime(time).
            if (existingKey.IsValid() && (existingKey.GetIndex() != keyHandle.GetIndex()))
            {
                // Save the old time before we set the new time so we
                // can reselect the m_keyHandle after the Delete.
                float currentTime = keyHandle.GetTime();

                if (!allowDeletingKeys) // Open dialog asking to allow deleting existing key - only once.
                {
                    // There is a bug in QT where editingFinished will get fired a second time if we show a QMessageBox
                    // so work around it by blocking signal before we do it.

                    ui->TIME->blockSignals(true);
                    QString msgBody = "There is an existing key at the specified time. If you continue, the existing key will be removed.";
                    if (QMessageBox::warning(this, "Overwrite Existing Key?", msgBody, QMessageBox::Cancel | QMessageBox::Yes) ==
                        QMessageBox::Cancel)
                    {
                        // Restore the old value and return.
                        ui->TIME->setValue(currentTime);
                        ui->TIME->blockSignals(false);
                        return;
                    }
                    else
                    {
                        allowDeletingKeys = true;
                        ui->TIME->blockSignals(false);
                    }
                }

                // Delete the key that is able to get replaced. This will
                // cause a sort and may cause m_keyHandle to become invalid.
                existingKey.Delete();

                // Reselect the key handle by time.
                keyHandle = track->GetKeyByTime(currentTime);

                keyHandle.SetTime(time);

                if (undoBatch)
                {
                    undoBatch->MarkEntityDirty(sequenceId);
                }

                m_selectedKeys = sequence->GetSelectedKeys(); // Re-save selected keys as key index has been changed.
            }
            else
            {
                keyHandle.SetTime(time);
                if (undoBatch)
                {
                    undoBatch->MarkEntityDirty(sequenceId);
                }
            }
        }
        else // During Undo/Redo
        {
            keyHandle.SetTime(time);
        }
    }
}

#include <TrackView/moc_TrackViewKeyPropertiesDlg.cpp>
