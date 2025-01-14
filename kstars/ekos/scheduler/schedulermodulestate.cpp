/*
    SPDX-FileCopyrightText: 2023 Wolfgang Reissenberger <sterne-jaeger@openfuture.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "schedulermodulestate.h"
#include <ekos_scheduler_debug.h>
#include "schedulerprocess.h"
#include "schedulerjob.h"
#include "kstarsdata.h"

#define MAX_FAILURE_ATTEMPTS 5

namespace Ekos
{
SchedulerModuleState::SchedulerModuleState() {}

void SchedulerModuleState::setCurrentProfile(const QString &newName, bool signal)
{
    bool changed = (newName != m_currentProfile);

    if (m_profiles.contains(newName))
        m_currentProfile = newName;
    else
    {
        changed = (m_currentProfile !=  m_profiles.first());
        m_currentProfile = m_profiles.first();
    }
    // update the UI
    if (signal && changed)
        emit currentProfileChanged();
}

void SchedulerModuleState::updateProfiles(const QStringList &newProfiles)
{
    QString selected = currentProfile();
    // Default profile is always the first one
    QStringList allProfiles(i18n("Default"));
    allProfiles.append(newProfiles);

    m_profiles = allProfiles;
    // ensure that the selected profile still exists
    setCurrentProfile(selected, false);
    emit profilesChanged();
}

void SchedulerModuleState::setStartupState(StartupState state)
{
    if (m_startupState != state)
    {
        m_startupState = state;
        emit startupStateChanged(state);
    }
}

void SchedulerModuleState::setShutdownState(ShutdownState state)
{
    if (m_shutdownState != state)
    {
        m_shutdownState = state;
        emit shutdownStateChanged(state);
    }
}

void SchedulerModuleState::setParkWaitState(ParkWaitState state)
{
    if (m_parkWaitState != state)
    {
        m_parkWaitState = state;
        emit parkWaitStateChanged(state);
    }
}

void SchedulerModuleState::enablePreemptiveShutdown(const QDateTime &wakeupTime)
{
    m_preemptiveShutdownWakeupTime = wakeupTime;
}

void SchedulerModuleState::disablePreemptiveShutdown()
{
    m_preemptiveShutdownWakeupTime = QDateTime();
}

const QDateTime &SchedulerModuleState::preemptiveShutdownWakeupTime() const
{
    return m_preemptiveShutdownWakeupTime;
}

bool SchedulerModuleState::preemptiveShutdown() const
{
    return m_preemptiveShutdownWakeupTime.isValid();
}

void SchedulerModuleState::setEkosState(EkosState state)
{
    if (m_ekosState != state)
    {
        qCDebug(KSTARS_EKOS_SCHEDULER) << "EKOS state changed from" << m_ekosState << "to" << state;
        m_ekosState = state;
        emit ekosStateChanged(state);
    }
}

bool SchedulerModuleState::increaseEkosConnectFailureCount()
{
    return (++m_ekosConnectFailureCount <= MAX_FAILURE_ATTEMPTS);
}

bool SchedulerModuleState::increaseParkingCapFailureCount()
{
    return (++m_parkingCapFailureCount <= MAX_FAILURE_ATTEMPTS);
}

bool SchedulerModuleState::increaseParkingMountFailureCount()
{
    return (++m_parkingMountFailureCount <= MAX_FAILURE_ATTEMPTS);
}

bool SchedulerModuleState::increaseParkingDomeFailureCount()
{
    return (++m_parkingDomeFailureCount <= MAX_FAILURE_ATTEMPTS);
}

void SchedulerModuleState::resetFailureCounters()
{
    resetIndiConnectFailureCount();
    resetEkosConnectFailureCount();
    resetFocusFailureCount();
    resetGuideFailureCount();
    resetAlignFailureCount();
    resetCaptureFailureCount();
}

bool SchedulerModuleState::increaseIndiConnectFailureCount()
{
    return (++m_indiConnectFailureCount <= MAX_FAILURE_ATTEMPTS);
}

bool SchedulerModuleState::increaseCaptureFailureCount()
{
    return (++m_captureFailureCount <= MAX_FAILURE_ATTEMPTS);
}

bool SchedulerModuleState::increaseFocusFailureCount()
{
    return (++m_focusFailureCount <= MAX_FAILURE_ATTEMPTS);
}

bool SchedulerModuleState::increaseGuideFailureCount()
{
    return (++m_guideFailureCount <= MAX_FAILURE_ATTEMPTS);
}

bool SchedulerModuleState::increaseAlignFailureCount()
{
    return (++m_alignFailureCount <= MAX_FAILURE_ATTEMPTS);
}

void SchedulerModuleState::setIndiState(INDIState state)
{
    if (m_indiState != state)
    {
        qCDebug(KSTARS_EKOS_SCHEDULER) << "INDI state changed from" << m_indiState << "to" << state;
        m_indiState = state;
        emit indiStateChanged(state);
    }
}

qint64 SchedulerModuleState::getCurrentOperationMsec() const
{
    if (!currentOperationTimeStarted) return 0;
    return currentOperationTime.msecsTo(KStarsData::Instance()->ut());
}

void SchedulerModuleState::startCurrentOperationTimer()
{
    currentOperationTimeStarted = true;
    currentOperationTime = KStarsData::Instance()->ut();
}

void SchedulerModuleState::cancelGuidingTimer()
{
    m_restartGuidingInterval = -1;
    m_restartGuidingTime = KStarsDateTime();
}

bool SchedulerModuleState::isGuidingTimerActive()
{
    return (m_restartGuidingInterval > 0 &&
            m_restartGuidingTime.msecsTo(KStarsData::Instance()->ut()) >= 0);
}

void SchedulerModuleState::startGuidingTimer(int milliseconds)
{
    m_restartGuidingInterval = milliseconds;
    m_restartGuidingTime = KStarsData::Instance()->ut();
}

// Allows for unit testing of static Scheduler methods,
// as can't call KStarsData::Instance() during unit testing.
KStarsDateTime *SchedulerModuleState::storedLocalTime = nullptr;
KStarsDateTime SchedulerModuleState::getLocalTime()
{
    if (hasLocalTime())
        return *storedLocalTime;
    return KStarsData::Instance()->geo()->UTtoLT(KStarsData::Instance()->clock()->utc());
}

void SchedulerModuleState::setupNextIteration(SchedulerTimerState nextState)
{
    setupNextIteration(nextState, m_UpdatePeriodMs);
}

void SchedulerModuleState::setupNextIteration(SchedulerTimerState nextState, int milliseconds)
{
    if (iterationSetup())
    {
        qCDebug(KSTARS_EKOS_SCHEDULER)
                << QString("Multiple setupNextIteration calls: current %1 %2, previous %3 %4")
                .arg(nextState).arg(milliseconds).arg(timerState()).arg(timerInterval());
    }
    setTimerState(nextState);
    // check if setup is called from a thread outside of the iteration timer thread
    if (iterationTimer().isActive())
    {
        // restart the timer to ensure the correct startup delay
        int remaining = iterationTimer().remainingTime();
        iterationTimer().stop();
        setTimerInterval(std::max(0, milliseconds - remaining));
        iterationTimer().start(timerInterval());
    }
    else
    {
        // setup called from inside the iteration timer thread
        setTimerInterval(milliseconds);
    }
    setIterationSetup(true);
}

uint SchedulerModuleState::maxFailureAttempts()
{
    return MAX_FAILURE_ATTEMPTS;
}
} // Ekos namespace
