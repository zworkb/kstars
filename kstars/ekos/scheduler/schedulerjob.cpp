/*  Ekos Scheduler Job
    SPDX-FileCopyrightText: Jasem Mutlaq <mutlaqja@ikarustech.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "schedulerjob.h"

#include "dms.h"
#include "artificialhorizoncomponent.h"
#include "kstarsdata.h"
#include "skymapcomposite.h"
#include "Options.h"
#include "scheduler.h"
#include "ksalmanac.h"

#include <knotification.h>

#include <QTableWidgetItem>

#include <ekos_scheduler_debug.h>

#define BAD_SCORE -1000
#define MIN_ALTITUDE 15.0

bool SchedulerJob::m_UpdateGraphics = true;

GeoLocation *SchedulerJob::storedGeo = nullptr;
KStarsDateTime *SchedulerJob::storedLocalTime = nullptr;
ArtificialHorizon *SchedulerJob::storedHorizon = nullptr;

QString SchedulerJob::jobStatusString(JOBStatus state)
{
    switch(state)
    {
        case SchedulerJob::JOB_IDLE:
            return "IDLE";
        case SchedulerJob::JOB_EVALUATION:
            return "EVAL";
        case SchedulerJob::JOB_SCHEDULED:
            return "SCHEDULED";
        case SchedulerJob::JOB_BUSY:
            return "BUSY";
        case SchedulerJob::JOB_ERROR:
            return "ERROR";
        case SchedulerJob::JOB_ABORTED:
            return "ABORTED";
        case SchedulerJob::JOB_INVALID:
            return "INVALID";
        case SchedulerJob::JOB_COMPLETE:
            return "COMPLETE";
    }
    return QString("????");
}

QString SchedulerJob::jobStageString(JOBStage state)
{
    switch(state)
    {
        case SchedulerJob::STAGE_IDLE:
            return "IDLE";
        case SchedulerJob::STAGE_SLEWING:
            return "SLEWING";
        case SchedulerJob::STAGE_SLEW_COMPLETE:
            return "SLEW_COMPLETE";
        case SchedulerJob::STAGE_FOCUSING:
            return "FOCUSING";
        case SchedulerJob::STAGE_FOCUS_COMPLETE:
            return "FOCUS_COMPLETE";
        case SchedulerJob::STAGE_ALIGNING:
            return "ALIGNING";
        case SchedulerJob::STAGE_ALIGN_COMPLETE:
            return "ALIGN_COMPLETE";
        case SchedulerJob::STAGE_RESLEWING:
            return "RESLEWING";
        case SchedulerJob::STAGE_RESLEWING_COMPLETE:
            return "RESLEWING_COMPLETE";
        case SchedulerJob::STAGE_POSTALIGN_FOCUSING:
            return "POSTALIGN_FOCUSING";
        case SchedulerJob::STAGE_POSTALIGN_FOCUSING_COMPLETE:
            return "POSTALIGN_FOCUSING_COMPLETE";
        case SchedulerJob::STAGE_GUIDING:
            return "GUIDING";
        case SchedulerJob::STAGE_GUIDING_COMPLETE:
            return "GUIDING_COMPLETE";
        case SchedulerJob::STAGE_CAPTURING:
            return "CAPTURING";
        case SchedulerJob::STAGE_COMPLETE:
            return "COMPLETE";
    }
    return QString("????");
}

QString SchedulerJob::startupConditionString(SchedulerJob::StartupCondition condition)
{
    switch(condition)
    {
        case START_ASAP:
            return "ASAP";
        case START_CULMINATION:
            return "CULM";
        case START_AT:
            return "AT";
    }
    return QString("????");
}

QString SchedulerJob::jobStartupConditionString(SchedulerJob::StartupCondition condition) const
{
    switch(condition)
    {
        case START_ASAP:
            return "ASAP";
        case START_CULMINATION:
            return "CULM";
        case START_AT:
            return QString("AT %1").arg(getFileStartupTime().toString("MM/dd hh:mm"));
    }
    return QString("????");
}

QString SchedulerJob::completionConditionString(SchedulerJob::CompletionCondition condition)
{
    switch(condition)
    {
        case FINISH_SEQUENCE:
            return "FINISH";
        case FINISH_REPEAT:
            return "REPEAT";
        case FINISH_LOOP:
            return "LOOP";
        case FINISH_AT:
            return "AT";
    }
    return QString("????");
}

QString SchedulerJob::jobCompletionConditionString(SchedulerJob::CompletionCondition condition) const
{
    switch(condition)
    {
        case FINISH_SEQUENCE:
            return "FINISH";
        case FINISH_REPEAT:
            return "REPEAT";
        case FINISH_LOOP:
            return "LOOP";
        case FINISH_AT:
            return QString("AT %1").arg(getCompletionTime().toString("MM/dd hh:mm"));
    }
    return QString("????");
}

SchedulerJob::SchedulerJob()
{
    moon = dynamic_cast<KSMoon *>(KStarsData::Instance()->skyComposite()->findByName(i18n("Moon")));
}

// Private constructor for unit testing.
SchedulerJob::SchedulerJob(KSMoon *moonPtr)
{
    moon = moonPtr;
}

void SchedulerJob::setName(const QString &value)
{
    name = value;
    updateJobCells();
}

KStarsDateTime SchedulerJob::getLocalTime()
{
    return Ekos::Scheduler::getLocalTime();
}

GeoLocation const *SchedulerJob::getGeo()
{
    if (hasGeo())
        return storedGeo;
    return KStarsData::Instance()->geo();
}

ArtificialHorizon const *SchedulerJob::getHorizon()
{
    if (hasHorizon())
        return storedHorizon;
    if (KStarsData::Instance() == nullptr || KStarsData::Instance()->skyComposite() == nullptr
            || KStarsData::Instance()->skyComposite()->artificialHorizon() == nullptr)
        return nullptr;
    return &KStarsData::Instance()->skyComposite()->artificialHorizon()->getHorizon();
}

void SchedulerJob::setStartupCondition(const StartupCondition &value)
{
    startupCondition = value;

    /* Keep startup time and condition valid */
    if (value == START_ASAP)
        startupTime = QDateTime();

    /* Refresh estimated time - which update job cells */
    setEstimatedTime(estimatedTime);

    /* Refresh dawn and dusk for startup date */
    calculateDawnDusk(startupTime, nextDawn, nextDusk);
}

void SchedulerJob::setStartupTime(const QDateTime &value)
{
    startupTime = value;

    /* Keep startup time and condition valid */
    if (value.isValid())
        startupCondition = START_AT;
    else
        startupCondition = fileStartupCondition;

    // Refresh altitude - invalid date/time is taken care of when rendering
    altitudeAtStartup = findAltitude(targetCoords, startupTime, &isSettingAtStartup);

    /* Refresh estimated time - which update job cells */
    setEstimatedTime(estimatedTime);

    /* Refresh dawn and dusk for startup date */
    calculateDawnDusk(startupTime, nextDawn, nextDusk);
}

void SchedulerJob::setSequenceFile(const QUrl &value)
{
    sequenceFile = value;
}

void SchedulerJob::setFITSFile(const QUrl &value)
{
    fitsFile = value;
}

void SchedulerJob::setMinAltitude(const double &value)
{
    minAltitude = value;
}

bool SchedulerJob::hasAltitudeConstraint() const
{
    return hasMinAltitude() ||
           (enforceArtificialHorizon && (getHorizon() != nullptr) && getHorizon()->altitudeConstraintsExist()) ||
           (Options::enableAltitudeLimits() &&
            (Options::minimumAltLimit() > 0 ||
             Options::maximumAltLimit() < 90));
}

void SchedulerJob::setMinMoonSeparation(const double &value)
{
    minMoonSeparation = value;
}

void SchedulerJob::setEnforceWeather(bool value)
{
    enforceWeather = value;
}

void SchedulerJob::setGreedyCompletionTime(const QDateTime &value)
{
    greedyCompletionTime = value;
}

void SchedulerJob::setCompletionTime(const QDateTime &value)
{
    setGreedyCompletionTime(QDateTime());

    /* If completion time is valid, automatically switch condition to FINISH_AT */
    if (value.isValid())
    {
        setCompletionCondition(FINISH_AT);
        completionTime = value;
        altitudeAtCompletion = findAltitude(targetCoords, completionTime, &isSettingAtCompletion);
        setEstimatedTime(-1);
    }
    /* If completion time is invalid, and job is looping, keep completion time undefined */
    else if (FINISH_LOOP == completionCondition)
    {
        completionTime = QDateTime();
        altitudeAtCompletion = findAltitude(targetCoords, completionTime, &isSettingAtCompletion);
        setEstimatedTime(-1);
    }
    /* If completion time is invalid, deduce completion from startup and duration */
    else if (startupTime.isValid())
    {
        completionTime = startupTime.addSecs(estimatedTime);
        altitudeAtCompletion = findAltitude(targetCoords, completionTime, &isSettingAtCompletion);
        updateJobCells();
    }
    /* Else just refresh estimated time - which update job cells */
    else setEstimatedTime(estimatedTime);


    /* Invariants */
    Q_ASSERT_X(completionTime.isValid() ?
               (FINISH_AT == completionCondition || FINISH_REPEAT == completionCondition || FINISH_SEQUENCE == completionCondition) :
               FINISH_LOOP == completionCondition,
               __FUNCTION__, "Valid completion time implies job is FINISH_AT/REPEAT/SEQUENCE, else job is FINISH_LOOP.");
}

void SchedulerJob::setCompletionCondition(const CompletionCondition &value)
{
    completionCondition = value;

    // Update repeats requirement, looping jobs have none
    switch (completionCondition)
    {
        case FINISH_LOOP:
            setCompletionTime(QDateTime());
        /* Fall through */
        case FINISH_AT:
            if (0 < getRepeatsRequired())
                setRepeatsRequired(0);
            break;

        case FINISH_SEQUENCE:
            if (1 != getRepeatsRequired())
                setRepeatsRequired(1);
            break;

        case FINISH_REPEAT:
            if (0 == getRepeatsRequired())
                setRepeatsRequired(1);
            break;

        default:
            break;
    }

    updateJobCells();
}

void SchedulerJob::setStepPipeline(const StepPipeline &value)
{
    stepPipeline = value;
}

void SchedulerJob::setState(const JOBStatus &value)
{
    state = value;
    stateTime = getLocalTime();

    /* FIXME: move this to Scheduler, SchedulerJob is mostly a model */
    if (JOB_ERROR == state)
    {
        lastErrorTime = getLocalTime();
        KNotification::event(QLatin1String("EkosSchedulerJobFail"), i18n("Ekos job failed (%1)", getName()));
    }

    /* If job becomes invalid or idle, automatically reset its startup characteristics, and force its duration to be reestimated */
    if (JOB_INVALID == value || JOB_IDLE == value)
    {
        setStartupCondition(fileStartupCondition);
        setStartupTime(fileStartupTime);
        setEstimatedTime(-1);
    }

    /* If job is aborted, automatically reset its startup characteristics */
    if (JOB_ABORTED == value)
    {
        lastAbortTime = getLocalTime();
        setStartupCondition(fileStartupCondition);
        /* setStartupTime(fileStartupTime); */
    }

    updateJobCells();
}


void SchedulerJob::setLeadTime(const int64_t &value)
{
    leadTime = value;
    updateJobCells();
}

void SchedulerJob::setScore(int value)
{
    score = value;
    updateJobCells();
}

void SchedulerJob::setCulminationOffset(const int16_t &value)
{
    culminationOffset = value;
}

void SchedulerJob::setSequenceCount(const int count)
{
    sequenceCount = count;
    updateJobCells();
}

void SchedulerJob::setNameCell(QTableWidgetItem *value)
{
    nameCell = value;
}

void SchedulerJob::setCompletedCount(const int count)
{
    completedCount = count;
    updateJobCells();
}

void SchedulerJob::setStatusCell(QTableWidgetItem *value)
{
    statusCell = value;
    if (nullptr != statusCell)
        statusCell->setToolTip(i18n("Current status of job '%1', managed by the Scheduler.\n"
                                    "If invalid, the Scheduler was not able to find a proper observation time for the target.\n"
                                    "If aborted, the Scheduler missed the scheduled time or encountered transitory issues and will reschedule the job.\n"
                                    "If complete, the Scheduler verified that all sequence captures requested were stored, including repeats.",
                                    name));
}

void SchedulerJob::setAltitudeCell(QTableWidgetItem *value)
{
    altitudeCell = value;
    if (nullptr != altitudeCell)
        altitudeCell->setToolTip(i18n("Current altitude of the target of job '%1'.\n"
                                      "A rising target is indicated with an arrow going up.\n"
                                      "A setting target is indicated with an arrow going down.",
                                      name));
}

void SchedulerJob::setStartupCell(QTableWidgetItem *value)
{
    startupCell = value;
    if (nullptr != startupCell)
        startupCell->setToolTip(i18n("Startup time for job '%1', as estimated by the Scheduler.\n"
                                     "The altitude at startup, if available, is displayed too.\n"
                                     "Fixed time from user or culmination time is marked with a chronometer symbol. ",
                                     name));
}

void SchedulerJob::setCompletionCell(QTableWidgetItem *value)
{
    completionCell = value;
    if (nullptr != completionCell)
        completionCell->setToolTip(i18n("Completion time for job '%1', as estimated by the Scheduler.\n"
                                        "You may specify a fixed time to limit duration of looping jobs. "
                                        "A warning symbol indicates the altitude at completion may cause the job to abort before completion.\n",
                                        name));
}

void SchedulerJob::setCaptureCountCell(QTableWidgetItem *value)
{
    captureCountCell = value;
    if (nullptr != captureCountCell)
        captureCountCell->setToolTip(i18n("Count of captures stored for job '%1', based on its sequence job.\n"
                                          "This is a summary, additional specific frame types may be required to complete the job.",
                                          name));
}

void SchedulerJob::setScoreCell(QTableWidgetItem *value)
{
    scoreCell = value;
    if (nullptr != scoreCell)
        scoreCell->setToolTip(i18n("Current score for job '%1', from its altitude, moon separation and sky darkness.\n"
                                   "Negative if adequate altitude is not achieved yet or if there is no proper observation time today.\n"
                                   "The Scheduler will refresh scores when picking a new candidate job.",
                                   name));
}

void SchedulerJob::setLeadTimeCell(QTableWidgetItem *value)
{
    leadTimeCell = value;
    if (nullptr != leadTimeCell)
        leadTimeCell->setToolTip(i18n("Time interval from the job which precedes job '%1'.\n"
                                      "Adjust the Lead Time in Ekos options to increase that duration and leave time for jobs to complete.\n"
                                      "Rearrange jobs to minimize that duration and optimize your imaging time.",
                                      name));
}

void SchedulerJob::setDateTimeDisplayFormat(const QString &value)
{
    dateTimeDisplayFormat = value;
    updateJobCells();
}

void SchedulerJob::setStage(const JOBStage &value)
{
    stage = value;
    updateJobCells();
}

void SchedulerJob::setStageCell(QTableWidgetItem *cell)
{
    stageCell = cell;
    // FIXME: Add a tool tip if cell is used
}

void SchedulerJob::setStageLabel(QLabel *label)
{
    stageLabel = label;
}

void SchedulerJob::setFileStartupCondition(const StartupCondition &value)
{
    fileStartupCondition = value;
}

void SchedulerJob::setFileStartupTime(const QDateTime &value)
{
    fileStartupTime = value;
}

void SchedulerJob::setEstimatedTime(const int64_t &value)
{
    /* Estimated time is generally the difference between startup and completion times:
     * - It is fixed when startup and completion times are fixed, that is, we disregard the argument
     * - Else mostly it pushes completion time from startup time
     *
     * However it cannot advance startup time when completion time is fixed because of the way jobs are scheduled.
     * This situation requires a warning in the user interface when there is not enough time for the job to process.
     */

    /* If startup and completion times are fixed, estimated time cannot change - disregard the argument */
    if (START_ASAP != fileStartupCondition && FINISH_AT == completionCondition)
    {
        estimatedTime = startupTime.secsTo(completionTime);
    }
    /* If completion time isn't fixed, estimated time adjusts completion time */
    else if (FINISH_AT != completionCondition && FINISH_LOOP != completionCondition)
    {
        estimatedTime = value;
        completionTime = startupTime.addSecs(value);
        altitudeAtCompletion = findAltitude(targetCoords, completionTime, &isSettingAtCompletion);
    }
    /* Else estimated time is simply stored as is - covers FINISH_LOOP from setCompletionTime */
    else estimatedTime = value;

    updateJobCells();
}

void SchedulerJob::setInSequenceFocus(bool value)
{
    inSequenceFocus = value;
}

void SchedulerJob::setPriority(const uint8_t &value)
{
    priority = value;
}

void SchedulerJob::setEnforceTwilight(bool value)
{
    enforceTwilight = value;
    calculateDawnDusk(startupTime, nextDawn, nextDusk);
}

void SchedulerJob::setEnforceArtificialHorizon(bool value)
{
    enforceArtificialHorizon = value;
}

void SchedulerJob::setEstimatedTimeCell(QTableWidgetItem *value)
{
    estimatedTimeCell = value;
    if (estimatedTimeCell)
        estimatedTimeCell->setToolTip(i18n("Duration job '%1' will take to complete when started, as estimated by the Scheduler.\n"
                                           "Depends on the actions to be run, and the sequence job to be processed.",
                                           name));
}

void SchedulerJob::setLightFramesRequired(bool value)
{
    lightFramesRequired = value;
}

void SchedulerJob::setRepeatsRequired(const uint16_t &value)
{
    repeatsRequired = value;

    // Update completion condition to be compatible
    if (1 < repeatsRequired)
    {
        if (FINISH_REPEAT != completionCondition)
            setCompletionCondition(FINISH_REPEAT);
    }
    else if (0 < repeatsRequired)
    {
        if (FINISH_SEQUENCE != completionCondition)
            setCompletionCondition(FINISH_SEQUENCE);
    }
    else
    {
        if (FINISH_LOOP != completionCondition)
            setCompletionCondition(FINISH_LOOP);
    }

    updateJobCells();
}

void SchedulerJob::setRepeatsRemaining(const uint16_t &value)
{
    repeatsRemaining = value;
    updateJobCells();
}

void SchedulerJob::setCapturedFramesMap(const CapturedFramesMap &value)
{
    capturedFramesMap = value;
}

void SchedulerJob::setTargetCoords(const dms &ra, const dms &dec, double djd)
{
    targetCoords.setRA0(ra);
    targetCoords.setDec0(dec);

    targetCoords.apparentCoord(static_cast<long double>(J2000), djd);
}

void SchedulerJob::setPositionAngle(double value)
{
    m_PositionAngle = value;
}

void SchedulerJob::updateJobCells()
{
    if (!m_UpdateGraphics) return;
    if (nullptr != nameCell)
    {
        nameCell->setText(name);
        if (nullptr != nameCell)
            nameCell->tableWidget()->resizeColumnToContents(nameCell->column());
    }

    if (nullptr != nameLabel)
    {
        nameLabel->setText(name + QString(":"));
    }

    if (nullptr != statusCell)
    {
        static QMap<JOBStatus, QString> stateStrings;
        static QString stateStringUnknown;
        if (stateStrings.isEmpty())
        {
            stateStrings[JOB_IDLE] = i18n("Idle");
            stateStrings[JOB_EVALUATION] = i18n("Evaluating");
            stateStrings[JOB_SCHEDULED] = i18n("Scheduled");
            stateStrings[JOB_BUSY] = i18n("Running");
            stateStrings[JOB_INVALID] = i18n("Invalid");
            stateStrings[JOB_COMPLETE] = i18n("Complete");
            stateStrings[JOB_ABORTED] = i18n("Aborted");
            stateStrings[JOB_ERROR] =  i18n("Error");
            stateStringUnknown = i18n("Unknown");
        }
        statusCell->setText(stateStrings.value(state, stateStringUnknown));

        if (nullptr != statusCell->tableWidget())
            statusCell->tableWidget()->resizeColumnToContents(statusCell->column());
    }

    if (nullptr != stageCell || nullptr != stageLabel)
    {
        /* Translated string cache - overkill, probably, and doesn't warn about missing enums like switch/case should ; also, not thread-safe */
        /* FIXME: this should work with a static initializer in C++11, but QT versions are touchy on this, and perhaps i18n can't be used? */
        static QMap<JOBStage, QString> stageStrings;
        static QString stageStringUnknown;
        if (stageStrings.isEmpty())
        {
            stageStrings[STAGE_IDLE] = i18n("Idle");
            stageStrings[STAGE_SLEWING] = i18n("Slewing");
            stageStrings[STAGE_SLEW_COMPLETE] = i18n("Slew complete");
            stageStrings[STAGE_FOCUSING] =
                stageStrings[STAGE_POSTALIGN_FOCUSING] = i18n("Focusing");
            stageStrings[STAGE_FOCUS_COMPLETE] =
                stageStrings[STAGE_POSTALIGN_FOCUSING_COMPLETE ] = i18n("Focus complete");
            stageStrings[STAGE_ALIGNING] = i18n("Aligning");
            stageStrings[STAGE_ALIGN_COMPLETE] = i18n("Align complete");
            stageStrings[STAGE_RESLEWING] = i18n("Repositioning");
            stageStrings[STAGE_RESLEWING_COMPLETE] = i18n("Repositioning complete");
            /*stageStrings[STAGE_CALIBRATING] = i18n("Calibrating");*/
            stageStrings[STAGE_GUIDING] = i18n("Guiding");
            stageStrings[STAGE_GUIDING_COMPLETE] = i18n("Guiding complete");
            stageStrings[STAGE_CAPTURING] = i18n("Capturing");
            stageStringUnknown = i18n("Unknown");
        }
        if (nullptr != stageCell)
        {
            stageCell->setText(stageStrings.value(stage, stageStringUnknown));
            if (nullptr != stageCell->tableWidget())
                stageCell->tableWidget()->resizeColumnToContents(stageCell->column());
        }
        if (nullptr != stageLabel)
        {
            stageLabel->setText(QString("%1: %2").arg(name, stageStrings.value(stage, stageStringUnknown)));
        }
    }

    if (nullptr != startupCell)
    {
        /* Display startup time if it is valid */
        if (startupTime.isValid())
        {
            startupCell->setText(QString("%1%2%L3° %4")
                                 .arg(altitudeAtStartup < minAltitude ? QString(QChar(0x26A0)) : "")
                                 .arg(QChar(isSettingAtStartup ? 0x2193 : 0x2191))
                                 .arg(altitudeAtStartup, 0, 'f', 1)
                                 .arg(startupTime.toString(dateTimeDisplayFormat)));

            switch (fileStartupCondition)
            {
                /* If the original condition is START_AT/START_CULMINATION, startup time is fixed */
                case START_AT:
                case START_CULMINATION:
                    startupCell->setIcon(QIcon::fromTheme("chronometer"));
                    break;

                /* If the original condition is START_ASAP, startup time is informational */
                case START_ASAP:
                    startupCell->setIcon(QIcon());
                    break;

                default:
                    break;
            }
        }
        /* Else do not display any startup time */
        else
        {
            startupCell->setText("-");
            startupCell->setIcon(QIcon());
        }

        if (nullptr != startupCell->tableWidget())
            startupCell->tableWidget()->resizeColumnToContents(startupCell->column());
    }

    if (nullptr != altitudeCell)
    {
        // FIXME: Cache altitude calculations
        bool is_setting = false;
        double const alt = findAltitude(targetCoords, QDateTime(), &is_setting);

        altitudeCell->setText(QString("%1%L2°")
                              .arg(QChar(is_setting ? 0x2193 : 0x2191))
                              .arg(alt, 0, 'f', 1));

        if (nullptr != altitudeCell->tableWidget())
            altitudeCell->tableWidget()->resizeColumnToContents(altitudeCell->column());
    }

    if (nullptr != completionCell)
    {
        if (greedyCompletionTime.isValid())
        {
            completionCell->setText(QString("%1")
                                    .arg(greedyCompletionTime.toString("hh:mm")));
        }
        else
            /* Display completion time if it is valid and job is not looping */
            if (FINISH_LOOP != completionCondition && completionTime.isValid())
            {
                completionCell->setText(QString("%1%2%L3° %4")
                                        .arg(altitudeAtCompletion < minAltitude ? QString(QChar(0x26A0)) : "")
                                        .arg(QChar(isSettingAtCompletion ? 0x2193 : 0x2191))
                                        .arg(altitudeAtCompletion, 0, 'f', 1)
                                        .arg(completionTime.toString(dateTimeDisplayFormat)));

                switch (completionCondition)
                {
                    case FINISH_AT:
                        completionCell->setIcon(QIcon::fromTheme("chronometer"));
                        break;

                    case FINISH_SEQUENCE:
                    case FINISH_REPEAT:
                    default:
                        completionCell->setIcon(QIcon());
                        break;
                }
            }
        /* Else do not display any completion time */
            else
            {
                completionCell->setText("-");
                completionCell->setIcon(QIcon());
            }

        if (nullptr != completionCell->tableWidget())
            completionCell->tableWidget()->resizeColumnToContents(completionCell->column());
    }

    if (nullptr != estimatedTimeCell)
    {
        if (0 < estimatedTime)
            /* Seconds to ms - this doesn't follow dateTimeDisplayFormat, which renders YMD too */
            estimatedTimeCell->setText(QTime::fromMSecsSinceStartOfDay(estimatedTime * 1000).toString("HH:mm:ss"));
#if 0
        else if(0 == estimatedTime)
            /* FIXME: this special case could be merged with the previous, kept for future to indicate actual duration */
            estimatedTimeCell->setText("00:00:00");
#endif
        else
            /* Invalid marker */
            estimatedTimeCell->setText("-");

        /* Warn the end-user if estimated time doesn't fit in the startup/completion interval */
        if (estimatedTime < startupTime.secsTo(completionTime))
            estimatedTimeCell->setIcon(QIcon::fromTheme("document-find"));
        else
            estimatedTimeCell->setIcon(QIcon());

        if (nullptr != estimatedTimeCell->tableWidget())
            estimatedTimeCell->tableWidget()->resizeColumnToContents(estimatedTimeCell->column());
    }

    if (nullptr != captureCountCell)
    {
        switch (completionCondition)
        {
            case FINISH_AT:
            // FIXME: Attempt to calculate the number of frames until end - requires detailed imaging time

            case FINISH_LOOP:
                // If looping, display the count of completed frames
                captureCountCell->setText(QString("%L1/-").arg(completedCount));
                break;

            case FINISH_SEQUENCE:
            case FINISH_REPEAT:
            default:
                // If repeating, display the count of completed frames to the count of requested frames
                captureCountCell->setText(QString("%L1/%L2").arg(completedCount).arg(sequenceCount));
                break;
        }

        if (nullptr != captureCountCell->tableWidget())
            captureCountCell->tableWidget()->resizeColumnToContents(captureCountCell->column());
    }

    if (nullptr != scoreCell)
    {
        if (0 <= score)
            scoreCell->setText(QString("%L1").arg(score));
        else
            /* FIXME: negative scores are just weird for the end-user */
            scoreCell->setText("<0");

        if (nullptr != scoreCell->tableWidget())
            scoreCell->tableWidget()->resizeColumnToContents(scoreCell->column());
    }

    if (nullptr != leadTimeCell)
    {
        // Display lead time, plus a warning if lead time is more than twice the lead time of the Ekos options
        switch (state)
        {
            case JOB_INVALID:
            case JOB_ERROR:
            case JOB_COMPLETE:
                leadTimeCell->setText("-");
                break;

            default:
                leadTimeCell->setText(QString("%1%2")
                                      .arg(Options::leadTime() * 60 * 2 < leadTime ? QString(QChar(0x26A0)) : "")
                                      .arg(QTime::fromMSecsSinceStartOfDay(leadTime * 1000).toString("HH:mm:ss")));
                break;
        }

        if (nullptr != leadTimeCell->tableWidget())
            leadTimeCell->tableWidget()->resizeColumnToContents(leadTimeCell->column());
    }
}

void SchedulerJob::reset()
{
    state = JOB_IDLE;
    stage = STAGE_IDLE;
    stateTime = getLocalTime();
    lastAbortTime = QDateTime();
    lastErrorTime = QDateTime();
    estimatedTime = -1;
    leadTime = 0;
    startupCondition = fileStartupCondition;
    startupTime = fileStartupCondition == START_AT ? fileStartupTime : QDateTime();

    /* Refresh dawn and dusk for startup date */
    calculateDawnDusk(startupTime, nextDawn, nextDusk);

    greedyCompletionTime = QDateTime();
    stopReason.clear();

    /* No change to culmination offset */
    repeatsRemaining = repeatsRequired;
    updateJobCells();
    clearCache();
}

bool SchedulerJob::decreasingScoreOrder(SchedulerJob const *job1, SchedulerJob const *job2)
{
    return job1->getScore() > job2->getScore();
}

bool SchedulerJob::increasingPriorityOrder(SchedulerJob const *job1, SchedulerJob const *job2)
{
    return job1->getPriority() < job2->getPriority();
}

bool SchedulerJob::decreasingAltitudeOrder(SchedulerJob const *job1, SchedulerJob const *job2, QDateTime const &when)
{
    bool A_is_setting = job1->isSettingAtStartup;
    double const altA = when.isValid() ?
                        findAltitude(job1->getTargetCoords(), when, &A_is_setting) :
                        job1->altitudeAtStartup;

    bool B_is_setting = job2->isSettingAtStartup;
    double const altB = when.isValid() ?
                        findAltitude(job2->getTargetCoords(), when, &B_is_setting) :
                        job2->altitudeAtStartup;

    // Sort with the setting target first
    if (A_is_setting && !B_is_setting)
        return true;
    else if (!A_is_setting && B_is_setting)
        return false;

    // If both targets rise or set, sort by decreasing altitude, considering a setting target is prioritary
    return (A_is_setting && B_is_setting) ? altA < altB : altB < altA;
}

bool SchedulerJob::increasingStartupTimeOrder(SchedulerJob const *job1, SchedulerJob const *job2)
{
    return job1->getStartupTime() < job2->getStartupTime();
}

bool SchedulerJob::satisfiesAltitudeConstraint(double azimuth, double altitude, QString *altitudeReason) const
{
    // Check the mount's altitude constraints.
    if (Options::enableAltitudeLimits() &&
            (altitude < Options::minimumAltLimit() ||
             altitude > Options::maximumAltLimit()))
    {
        if (altitudeReason != nullptr)
        {
            if (altitude < Options::minimumAltLimit())
                *altitudeReason = QString("altitude %1 < mount altitude limit %2")
                                  .arg(altitude, 0, 'f', 1).arg(Options::minimumAltLimit(), 0, 'f', 1);
            else
                *altitudeReason = QString("altitude %1 > mount altitude limit %2")
                                  .arg(altitude, 0, 'f', 1).arg(Options::maximumAltLimit(), 0, 'f', 1);
        }
        return false;
    }
    // Check the global min-altitude constraint.
    if (altitude < getMinAltitude())
    {
        if (altitudeReason != nullptr)
            *altitudeReason = QString("altitude %1 < minAltitude %2").arg(altitude, 0, 'f', 1).arg(getMinAltitude(), 0, 'f', 1);
        return false;
    }
    // Check the artificial horizon.
    if (getHorizon() != nullptr && enforceArtificialHorizon)
        return getHorizon()->isAltitudeOK(azimuth, altitude, altitudeReason);

    return true;
}

int16_t SchedulerJob::getAltitudeScore(QDateTime const &when, double *altPtr) const
{
    // FIXME: block calculating target coordinates at a particular time is duplicated in several places

    // Retrieve the argument date/time, or fall back to current time - don't use QDateTime's timezone!
    KStarsDateTime ltWhen(when.isValid() ?
                          Qt::UTC == when.timeSpec() ? getGeo()->UTtoLT(KStarsDateTime(when)) : when :
                          getLocalTime());

    // Create a sky object with the target catalog coordinates
    SkyPoint const target = getTargetCoords();
    SkyObject o;
    o.setRA0(target.ra0());
    o.setDec0(target.dec0());

    // Update RA/DEC of the target for the current fraction of the day
    KSNumbers numbers(ltWhen.djd());
    o.updateCoordsNow(&numbers);

    // Compute local sidereal time for the current fraction of the day, calculate altitude
    CachingDms const LST = getGeo()->GSTtoLST(getGeo()->LTtoUT(ltWhen).gst());
    o.EquatorialToHorizontal(&LST, getGeo()->lat());
    double const altitude = o.alt().Degrees();
    double const azimuth = o.az().Degrees();
    if (altPtr != nullptr)
        *altPtr = altitude;

    double const SETTING_ALTITUDE_CUTOFF = Options::settingAltitudeCutoff();
    int16_t score = BAD_SCORE - 1;

    bool const altitudeOK = satisfiesAltitudeConstraint(azimuth, altitude);

    // If altitude is negative, bad score
    // FIXME: some locations may allow negative altitudes
    if (altitude < 0)
    {
        score = BAD_SCORE;
    }
    else if (hasAltitudeConstraint())
    {
        // If under altitude constraint, bad score
        if (!altitudeOK)
            score = BAD_SCORE;
        // Else if setting and under altitude cutoff, job would end soon after starting, bad score
        // FIXME: half bad score when under altitude cutoff risk getting positive again
        else
        {
            double offset = LST.Hours() - o.ra().Hours();
            if (24.0 <= offset)
                offset -= 24.0;
            else if (offset < 0.0)
                offset += 24.0;
            if (0.0 <= offset && offset < 12.0)
            {
                bool const settingAltitudeOK = satisfiesAltitudeConstraint(azimuth, altitude - SETTING_ALTITUDE_CUTOFF);
                if (!settingAltitudeOK)
                    score = BAD_SCORE / 2;
            }
        }
    }
    // If not constrained but below minimum hard altitude, set score to 10% of altitude value
    else if (altitude < MIN_ALTITUDE)
    {
        score = static_cast <int16_t> (altitude / 10.0);
    }

    // Else default score calculation without altitude constraint
    if (score < BAD_SCORE)
        score = static_cast <int16_t> ((1.5 * pow(1.06, altitude)) - (MIN_ALTITUDE / 10.0));

    //qCDebug(KSTARS_EKOS_SCHEDULER) << QString("Job '%1' target altitude is %3 degrees at %2 (score %4).")
    //    .arg(getName())
    //    .arg(when.toString(getDateTimeDisplayFormat()))
    //    .arg(currentAlt, 0, 'f', minAltitude->decimals())
    //    .arg(QString::asprintf("%+d", score));

    return score;
}

int16_t SchedulerJob::getMoonSeparationScore(QDateTime const &when) const
{
    if (moon == nullptr) return 100;

    // FIXME: block calculating target coordinates at a particular time is duplicated in several places

    // Retrieve the argument date/time, or fall back to current time - don't use QDateTime's timezone!
    KStarsDateTime ltWhen(when.isValid() ?
                          Qt::UTC == when.timeSpec() ? getGeo()->UTtoLT(KStarsDateTime(when)) : when :
                          getLocalTime());

    // Create a sky object with the target catalog coordinates
    SkyPoint const target = getTargetCoords();
    SkyObject o;
    o.setRA0(target.ra0());
    o.setDec0(target.dec0());

    // Update RA/DEC of the target for the current fraction of the day
    KSNumbers numbers(ltWhen.djd());
    o.updateCoordsNow(&numbers);

    // Update moon
    //ut = getGeo()->LTtoUT(ltWhen);
    //KSNumbers ksnum(ut.djd()); // BUG: possibly LT.djd() != UT.djd() because of translation
    //LST = getGeo()->GSTtoLST(ut.gst());
    CachingDms LST = getGeo()->GSTtoLST(getGeo()->LTtoUT(ltWhen).gst());
    moon->updateCoords(&numbers, true, getGeo()->lat(), &LST, true);

    double const moonAltitude = moon->alt().Degrees();

    // Lunar illumination %
    double const illum = moon->illum() * 100.0;

    // Moon/Sky separation p
    double const separation = moon->angularDistanceTo(&o).Degrees();

    // Zenith distance of the moon
    double const zMoon = (90 - moonAltitude);
    // Zenith distance of target
    double const zTarget = (90 - o.alt().Degrees());

    int16_t score = 0;

    // If target = Moon, or no illuminiation, or moon below horizon, return static score.
    if (zMoon == zTarget || illum == 0 || zMoon >= 90)
        score = 100;
    else
    {
        // JM: Some magic voodoo formula I came up with!
        double moonEffect = (pow(separation, 1.7) * pow(zMoon, 0.5)) / (pow(zTarget, 1.1) * pow(illum, 0.5));

        // Limit to 0 to 100 range.
        moonEffect = KSUtils::clamp(moonEffect, 0.0, 100.0);

        if (getMinMoonSeparation() > 0)
        {
            if (separation < getMinMoonSeparation())
                score = BAD_SCORE * 5;
            else
                score = moonEffect;
        }
        else
            score = moonEffect;
    }

    // Limit to 0 to 20
    score /= 5.0;

    //qCDebug(KSTARS_EKOS_SCHEDULER) << QString("Job '%1' target is %L3 degrees from Moon (score %2).")
    //    .arg(getName())
    //    .arg(separation, 0, 'f', 3)
    //    .arg(QString::asprintf("%+d", score));

    return score;
}


double SchedulerJob::getCurrentMoonSeparation() const
{
    if (moon == nullptr) return 180.0;

    // FIXME: block calculating target coordinates at a particular time is duplicated in several places

    // Retrieve the argument date/time, or fall back to current time - don't use QDateTime's timezone!
    KStarsDateTime ltWhen(getLocalTime());

    // Create a sky object with the target catalog coordinates
    SkyPoint const target = getTargetCoords();
    SkyObject o;
    o.setRA0(target.ra0());
    o.setDec0(target.dec0());

    // Update RA/DEC of the target for the current fraction of the day
    KSNumbers numbers(ltWhen.djd());
    o.updateCoordsNow(&numbers);

    // Update moon
    //ut = getGeo()->LTtoUT(ltWhen);
    //KSNumbers ksnum(ut.djd()); // BUG: possibly LT.djd() != UT.djd() because of translation
    //LST = getGeo()->GSTtoLST(ut.gst());
    CachingDms LST = getGeo()->GSTtoLST(getGeo()->LTtoUT(ltWhen).gst());
    moon->updateCoords(&numbers, true, getGeo()->lat(), &LST, true);

    // Moon/Sky separation p
    return moon->angularDistanceTo(&o).Degrees();
}

QDateTime SchedulerJob::calculateNextTime(QDateTime const &when, bool checkIfConstraintsAreMet, int increment,
        QString *reason, bool runningJob, const QDateTime &until) const
{
    // FIXME: block calculating target coordinates at a particular time is duplicated in several places

    // Retrieve the argument date/time, or fall back to current time - don't use QDateTime's timezone!
    KStarsDateTime ltWhen(when.isValid() ?
                          Qt::UTC == when.timeSpec() ? getGeo()->UTtoLT(KStarsDateTime(when)) : when :
                          getLocalTime());

    // Create a sky object with the target catalog coordinates
    SkyPoint const target = getTargetCoords();
    SkyObject o;
    o.setRA0(target.ra0());
    o.setDec0(target.dec0());

    // Calculate the UT at the argument time
    KStarsDateTime const ut = getGeo()->LTtoUT(ltWhen);

    double const SETTING_ALTITUDE_CUTOFF = Options::settingAltitudeCutoff();

    auto maxMinute = 1e8;
    if (!runningJob && until.isValid())
        maxMinute = when.secsTo(until) / 60;

    if (maxMinute > 24 * 60)
        maxMinute = 24 * 60;

    // Within the next 24 hours, search when the job target matches the altitude and moon constraints
    for (unsigned int minute = 0; minute < maxMinute; minute += increment)
    {
        KStarsDateTime const ltOffset(ltWhen.addSecs(minute * 60));

        // Is this violating twilight?
        QDateTime nextSuccess;
        if (getEnforceTwilight() && !runsDuringAstronomicalNightTime(ltOffset, &nextSuccess))
        {
            if (checkIfConstraintsAreMet)
            {
                // Change the minute to increment-minutes before next success.
                if (nextSuccess.isValid())
                {
                    const int minutesToSuccess = ltOffset.secsTo(nextSuccess) / 60 - increment;
                    if (minutesToSuccess > 0)
                        minute += minutesToSuccess;
                }
                continue;
            }
            else
            {
                if (reason) *reason = "twilight";
                return ltOffset;
            }
        }

        // Update RA/DEC of the target for the current fraction of the day
        KSNumbers numbers(ltOffset.djd());
        o.updateCoordsNow(&numbers);

        // Compute local sidereal time for the current fraction of the day, calculate altitude
        CachingDms const LST = getGeo()->GSTtoLST(getGeo()->LTtoUT(ltOffset).gst());
        o.EquatorialToHorizontal(&LST, getGeo()->lat());
        double const altitude = o.alt().Degrees();
        double const azimuth = o.az().Degrees();

        bool const altitudeOK = satisfiesAltitudeConstraint(azimuth, altitude, reason);
        if (altitudeOK)
        {
            // Don't test proximity to dawn in this situation, we only cater for altitude here

            // Continue searching if Moon separation is not good enough
            if (0 < getMinMoonSeparation() && getMoonSeparationScore(ltOffset) < 0)
            {
                if (checkIfConstraintsAreMet)
                    continue;
                else
                {
                    if (reason) *reason = QString("moon separation");
                    return ltOffset;
                }
            }

            // Continue searching if target is setting and under the cutoff
            if (checkIfConstraintsAreMet)
            {
                if (!runningJob)
                {
                    double offset = LST.Hours() - o.ra().Hours();
                    if (24.0 <= offset)
                        offset -= 24.0;
                    else if (offset < 0.0)
                        offset += 24.0;
                    if (0.0 <= offset && offset < 12.0)
                    {
                        bool const settingAltitudeOK = satisfiesAltitudeConstraint(azimuth, altitude - SETTING_ALTITUDE_CUTOFF);
                        if (!settingAltitudeOK)
                            continue;
                    }
                }
                return ltOffset;
            }
        }
        else if (!checkIfConstraintsAreMet)
            return ltOffset;
    }

    return QDateTime();
}

QDateTime SchedulerJob::calculateCulmination(QDateTime const &when) const
{
    // FIXME: culmination calculation is a min altitude requirement, should be an interval altitude requirement

    // FIXME: block calculating target coordinates at a particular time is duplicated in calculateCulmination

    // Retrieve the argument date/time, or fall back to current time - don't use QDateTime's timezone!
    KStarsDateTime ltWhen(when.isValid() ?
                          Qt::UTC == when.timeSpec() ? getGeo()->UTtoLT(KStarsDateTime(when)) : when :
                          getLocalTime());

    // Create a sky object with the target catalog coordinates
    SkyPoint const target = getTargetCoords();
    SkyObject o;
    o.setRA0(target.ra0());
    o.setDec0(target.dec0());

    // Update RA/DEC for the argument date/time
    KSNumbers numbers(ltWhen.djd());
    o.updateCoordsNow(&numbers);

    // Calculate transit date/time at the argument date - transitTime requires UT and returns LocalTime
    KStarsDateTime transitDateTime(ltWhen.date(), o.transitTime(getGeo()->LTtoUT(ltWhen), getGeo()), Qt::LocalTime);

    // Shift transit date/time by the argument offset
    KStarsDateTime observationDateTime = transitDateTime.addSecs(getCulminationOffset() * 60);

    // Relax observation time, culmination calculation is stable at minute only
    KStarsDateTime relaxedDateTime = observationDateTime.addSecs(Options::leadTime() * 60);

    // Verify resulting observation time is under lead time vs. argument time
    // If sooner, delay by 8 hours to get to the next transit - perhaps in a third call
    if (relaxedDateTime < ltWhen)
    {
        qCDebug(KSTARS_EKOS_SCHEDULER) << QString("Job '%1' startup %2 is posterior to transit %3, shifting by 8 hours.")
                                       .arg(getName())
                                       .arg(ltWhen.toString(getDateTimeDisplayFormat()))
                                       .arg(relaxedDateTime.toString(getDateTimeDisplayFormat()));

        return calculateCulmination(when.addSecs(8 * 60 * 60));
    }

    // Guarantees - culmination calculation is stable at minute level, so relax by lead time
    Q_ASSERT_X(observationDateTime.isValid(), __FUNCTION__, "Observation time for target culmination is valid.");
    Q_ASSERT_X(ltWhen <= relaxedDateTime, __FUNCTION__,
               "Observation time for target culmination is at or after than argument time");

    // Return consolidated culmination time
    return Qt::UTC == observationDateTime.timeSpec() ? getGeo()->UTtoLT(observationDateTime) : observationDateTime;
}

double SchedulerJob::findAltitude(const SkyPoint &target, const QDateTime &when, bool * is_setting, bool debug)
{
    // FIXME: block calculating target coordinates at a particular time is duplicated in several places

    // Retrieve the argument date/time, or fall back to current time - don't use QDateTime's timezone!
    KStarsDateTime ltWhen(when.isValid() ?
                          Qt::UTC == when.timeSpec() ? getGeo()->UTtoLT(KStarsDateTime(when)) : when :
                          getLocalTime());

    // Create a sky object with the target catalog coordinates
    SkyObject o;
    o.setRA0(target.ra0());
    o.setDec0(target.dec0());

    // Update RA/DEC of the target for the current fraction of the day
    KSNumbers numbers(ltWhen.djd());
    o.updateCoordsNow(&numbers);

    // Calculate alt/az coordinates using KStars instance's geolocation
    CachingDms const LST = getGeo()->GSTtoLST(getGeo()->LTtoUT(ltWhen).gst());
    o.EquatorialToHorizontal(&LST, getGeo()->lat());

    // Hours are reduced to [0,24[, meridian being at 0
    double offset = LST.Hours() - o.ra().Hours();
    if (24.0 <= offset)
        offset -= 24.0;
    else if (offset < 0.0)
        offset += 24.0;
    bool const passed_meridian = 0.0 <= offset && offset < 12.0;

    if (debug)
        qCDebug(KSTARS_EKOS_SCHEDULER) << QString("When:%9 LST:%8 RA:%1 RA0:%2 DEC:%3 DEC0:%4 alt:%5 setting:%6 HA:%7")
                                       .arg(o.ra().toHMSString())
                                       .arg(o.ra0().toHMSString())
                                       .arg(o.dec().toHMSString())
                                       .arg(o.dec0().toHMSString())
                                       .arg(o.alt().Degrees())
                                       .arg(passed_meridian ? "yes" : "no")
                                       .arg(o.ra().Hours())
                                       .arg(LST.toHMSString())
                                       .arg(ltWhen.toString("HH:mm:ss"));

    if (is_setting)
        *is_setting = passed_meridian;

    return o.alt().Degrees();
}

void SchedulerJob::calculateDawnDusk(QDateTime const &when, QDateTime &nDawn, QDateTime &nDusk)
{
    QDateTime startup = when;

    if (!startup.isValid())
        startup = getLocalTime();

    // Our local midnight - the KStarsDateTime date+time constructor is safe for local times
    // Exact midnight seems unreliable--offset it by a minute.
    KStarsDateTime midnight(startup.date(), QTime(0, 1), Qt::LocalTime);

    QDateTime dawn = startup, dusk = startup;

    // Loop dawn and dusk calculation until the events found are the next events
    for ( ; dawn <= startup || dusk <= startup ; midnight = midnight.addDays(1))
    {
        // KSAlmanac computes the closest dawn and dusk events from the local sidereal time corresponding to the midnight argument

#if 0
        KSAlmanac const ksal(midnight, getGeo());
        // If dawn is in the past compared to this observation, fetch the next dawn
        if (dawn <= startup)
            dawn = getGeo()->UTtoLT(ksal.getDate().addSecs((ksal.getDawnAstronomicalTwilight() * 24.0 + Options::dawnOffset()) *
                                    3600.0));
        // If dusk is in the past compared to this observation, fetch the next dusk
        if (dusk <= startup)
            dusk = getGeo()->UTtoLT(ksal.getDate().addSecs((ksal.getDuskAstronomicalTwilight() * 24.0 + Options::duskOffset()) *
                                    3600.0));
#else
        // Creating these almanac instances seems expensive.
        static QMap<QString, KSAlmanac const * > almanacMap;
        const QString key = QString("%1 %2 %3").arg(midnight.toString()).arg(getGeo()->lat()->Degrees()).arg(
                                getGeo()->lng()->Degrees());
        KSAlmanac const * ksal = almanacMap.value(key, nullptr);
        if (ksal == nullptr)
        {
            if (almanacMap.size() > 5)
            {
                // don't allow this to grow too large.
                qDeleteAll(almanacMap);
                almanacMap.clear();
            }
            ksal = new KSAlmanac(midnight, getGeo());
            almanacMap[key] = ksal;
        }

        // If dawn is in the past compared to this observation, fetch the next dawn
        if (dawn <= startup)
            dawn = getGeo()->UTtoLT(ksal->getDate().addSecs((ksal->getDawnAstronomicalTwilight() * 24.0 + Options::dawnOffset()) *
                                    3600.0));

        // If dusk is in the past compared to this observation, fetch the next dusk
        if (dusk <= startup)
            dusk = getGeo()->UTtoLT(ksal->getDate().addSecs((ksal->getDuskAstronomicalTwilight() * 24.0 + Options::duskOffset()) *
                                    3600.0));
#endif
    }

    // Now we have the next events:
    // - if dawn comes first, observation runs during the night
    // - if dusk comes first, observation runs during the day
    nDawn = dawn;
    nDusk = dusk;
}

bool SchedulerJob::runsDuringAstronomicalNightTime(const QDateTime &time,
        QDateTime *nextPossibleSuccess) const
{
    // We call this very frequently in the Greedy Algorithm, and the calls
    // below are expensive. Almost all the calls are redundent (e.g. if it's not nighttime
    // now, it's not nighttime in 10 minutes). So, cache the answer and return it if the next
    // call is for a time between this time and the next dawn/dusk (whichever is sooner).

    static QDateTime previousMinDawnDusk, previousTime;
    static GeoLocation const *previousGeo = nullptr;  // A dangling pointer, I suppose, but we never reference it.
    static bool previousAnswer;
    static double previousPreDawnTime = 0;
    static QDateTime nextSuccess;

    // Lock this method because of all the statics
    static std::mutex nightTimeMutex;
    const std::lock_guard<std::mutex> lock(nightTimeMutex);

    // We likely can rely on the previous calculations.
    if (previousTime.isValid() && previousMinDawnDusk.isValid() &&
            time >= previousTime && time < previousMinDawnDusk &&
            getGeo() == previousGeo &&
            Options::preDawnTime() == previousPreDawnTime)
    {
        if (!previousAnswer && nextPossibleSuccess != nullptr)
            *nextPossibleSuccess = nextSuccess;
        return previousAnswer;
    }
    else
    {
        previousAnswer = runsDuringAstronomicalNightTimeInternal(time, &previousMinDawnDusk, &nextSuccess);
        previousTime = time;
        previousGeo = getGeo();
        previousPreDawnTime = Options::preDawnTime();
        if (!previousAnswer && nextPossibleSuccess != nullptr)
            *nextPossibleSuccess = nextSuccess;
        return previousAnswer;
    }
}


bool SchedulerJob::runsDuringAstronomicalNightTimeInternal(const QDateTime &time, QDateTime *minDawnDusk,
        QDateTime *nextPossibleSuccess) const
{
    QDateTime t;
    QDateTime nDawn = nextDawn, nDusk = nextDusk;
    if (time.isValid())
    {
        // Can't rely on the pre-computed dawn/dusk if we're giving it an arbitary time.
        calculateDawnDusk(time, nDawn, nDusk);
        t = time;
    }
    else
    {
        t = startupTime;
    }

    // Calculate the next astronomical dawn time, adjusted with the Ekos pre-dawn offset
    QDateTime const earlyDawn = nDawn.addSecs(-60.0 * abs(Options::preDawnTime()));

    *minDawnDusk = earlyDawn < nDusk ? earlyDawn : nDusk;

    // Dawn and dusk are ordered as the immediate next events following the observation time
    // Thus if dawn comes first, the job startup time occurs during the dusk/dawn interval.
    bool result = nDawn < nDusk && t <= earlyDawn;

    // Return a hint about when it might succeed.
    if (nextPossibleSuccess != nullptr)
    {
        if (result) *nextPossibleSuccess = QDateTime();
        else *nextPossibleSuccess = nDusk;
    }

    return result;
}

void SchedulerJob::setInitialFilter(const QString &value)
{
    m_InitialFilter = value;
}

const QString &SchedulerJob::getInitialFilter() const
{
    return m_InitialFilter;
}

bool SchedulerJob::StartTimeCache::check(const QDateTime &from, const QDateTime &until,
        QDateTime *result, QDateTime *newFrom) const
{
    // Look at the cached results from getNextPossibleStartTime.
    // If the desired 'from' time is in one of them, that is, between computation.from and computation.until,
    // then we can re-use that result (as long as the desired until time is < computation.until).
    foreach (const StartTimeComputation &computation, startComputations)
    {
        if (from >= computation.from &&
                (!computation.until.isValid() || from < computation.until) &&
                (!computation.result.isValid() || from < computation.result))
        {
            if (computation.result.isValid() || until <= computation.until)
            {
                // We have a cached result.
                *result = computation.result;
                *newFrom = QDateTime();
                return true;
            }
            else
            {
                // No cached result, but at least we can constrain the search.
                *result = QDateTime();
                *newFrom = computation.until;
                return true;
            }
        }
    }
    return false;
}

void SchedulerJob::StartTimeCache::clear() const
{
    startComputations.clear();
}

void SchedulerJob::StartTimeCache::add(const QDateTime &from, const QDateTime &until, const QDateTime &result) const
{
    // Manage the cache size.
    if (startComputations.size() > 10)
        startComputations.clear();

    // The getNextPossibleStartTime computation (which calls calculateNextTime) searches ahead at most 24 hours.
    QDateTime endTime;
    if (!until.isValid())
        endTime = from.addSecs(24 * 3600);
    else
    {
        QDateTime oneDay = from.addSecs(24 * 3600);
        if (until > oneDay)
            endTime = oneDay;
        else
            endTime = until;
    }

    StartTimeComputation c;
    c.from = from;
    c.until = endTime;
    c.result = result;
    startComputations.push_back(c);
}

// When can this job start? For now ignores culmination constraint.
QDateTime SchedulerJob::getNextPossibleStartTime(const QDateTime &when, int increment, bool runningJob,
        const QDateTime &until) const
{
    QDateTime ltWhen(
        when.isValid() ? (Qt::UTC == when.timeSpec() ? getGeo()->UTtoLT(KStarsDateTime(when)) : when)
        : getLocalTime());

    // We do not consider job state here. It is the responsibility of the caller
    // to filter for that, if desired.

    if (SchedulerJob::START_AT == getFileStartupCondition())
    {
        int secondsFromNow = ltWhen.secsTo(getFileStartupTime());
        if (secondsFromNow < -500)
            // We missed it.
            return QDateTime();
        ltWhen = secondsFromNow > 0 ? getFileStartupTime() : ltWhen;
    }

    // Can't start if we're past the finish time.
    if (getCompletionCondition() == FINISH_AT)
    {
        const QDateTime &t = getCompletionTime();
        if (t.isValid() && t < ltWhen)
            return QDateTime(); // return an invalid time.
    }

    if (runningJob)
        return calculateNextTime(ltWhen, true, increment, nullptr, runningJob, until);
    else
    {
        QDateTime result, newFrom;
        if (startTimeCache.check(ltWhen, until, &result, &newFrom))
        {
            if (result.isValid() || !newFrom.isValid())
                return result;
            if (newFrom.isValid())
                ltWhen = newFrom;
        }
        result = calculateNextTime(ltWhen, true, increment, nullptr, runningJob, until);
        startTimeCache.add(ltWhen, until, result);
        return result;
    }
}

// When will this job end (not looking at capture plan)?
QDateTime SchedulerJob::getNextEndTime(const QDateTime &start, int increment, QString *reason, const QDateTime &until) const
{
    QDateTime ltStart(
        start.isValid() ? (Qt::UTC == start.timeSpec() ? getGeo()->UTtoLT(KStarsDateTime(start)) : start)
        : getLocalTime());

    // We do not consider job state here. It is the responsibility of the caller
    // to filter for that, if desired.

    if (SchedulerJob::START_AT == getFileStartupCondition())
    {
        if (getFileStartupTime().secsTo(ltStart) < 60)
        {
            // if the file startup time is in the future, then end now.
            // This case probably wouldn't happen in the running code.
            if (reason) *reason = "before start-at time";
            return QDateTime();
        }
        // otherwise, test from now.
    }

    // Can't start if we're past the finish time.
    if (getCompletionCondition() == FINISH_AT)
    {
        const QDateTime &t = getCompletionTime();
        if (t.isValid() && t < ltStart)
        {
            if (reason) *reason = "end-at time";
            return QDateTime(); // return an invalid time.
        }
        auto result = calculateNextTime(ltStart, false, increment, reason, false, until);
        if (!result.isValid() || result.secsTo(getCompletionTime()) < 0)
        {
            if (reason) *reason = "end-at time";
            return getCompletionTime();
        }
        else return result;
    }

    return calculateNextTime(ltStart, false, increment, reason, false, until);
}

QJsonObject SchedulerJob::toJson() const
{
    return
    {
        {"name", name},
        {"pa", m_PositionAngle},
        {"targetRA", targetCoords.ra0().Hours()},
        {"targetDEC", targetCoords.dec0().Degrees()},
        {"state", state},
        {"stage", stage},
        {"sequenceCount", sequenceCount},
        {"completedCount", completedCount},
        {"minAltitude", minAltitude},
        {"minMoonSeparation", minMoonSeparation},
        // Warning: Qt JSON does not natively support 64bit integers
        {"estimatedTime", static_cast<double>(estimatedTime)},
        {"culminationOffset", culminationOffset},
        {"priority", priority},
        // Warning: Qt JSON does not natively support 64bit integers
        {"leadTime", static_cast<double>(leadTime)},
        {"repeatsRequired", repeatsRequired},
        {"repeatsRemaining", repeatsRemaining},
        {"inSequenceFocus", inSequenceFocus},
        {"score", score},

    };
}
