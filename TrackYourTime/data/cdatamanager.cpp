/*
 * TrackYourTime - cross-platform time tracker
 * Copyright (C) 2015-2017  Alexander Basov <basovav@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "cdatamanager.h"
#include <QDebug>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include "../tools/tools.h"
#include "../tools/os_api.h"
#include "../tools/cfilebin.h"
#include "cdbversionconverter.h"
#include "capppredefinedinfo.h"
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

const QString cDataManager::CONF_UPDATE_DELAY_ID = "UPDATE_DELAY";
const QString cDataManager::CONF_IDLE_DELAY_ID = "IDLE_DELAY";
const QString cDataManager::CONF_AUTOSAVE_DELAY_ID = "AUTOSAVE_DELAY";
const QString cDataManager::CONF_STORAGE_FILENAME_ID = "STORAGE_FILENAME";
const QString cDataManager::CONF_LANGUAGE_ID = "LANGUAGE";
const QString cDataManager::CONF_FIRST_LAUNCH_ID = "FIRST_LAUNCH";
const QString cDataManager::CONF_NOTIFICATION_SHOW_SYSTEM_ID = "NOTIFICATION_SHOW_SYSTEM";
const QString cDataManager::CONF_NOTIFICATION_MESSAGE_ID = "NOTIFICATION_MESSAGE";
const QString cDataManager::CONF_NOTIFICATION_HIDE_SECONDS_ID = "NOTIFICATION_HIDE_SECONDS";
const QString cDataManager::CONF_NOTIFICATION_POSITION_ID = "NOTIFICATION_POSITION";
const QString cDataManager::CONF_NOTIFICATION_SIZE_ID = "NOTIFICATION_SIZE";
const QString cDataManager::CONF_NOTIFICATION_OPACITY_ID = "NOTIFICATION_OPACITY";
const QString cDataManager::CONF_NOTIFICATION_MOUSE_BEHAVIOR_ID = "NOTIFICATION_MOUSE_BEHAVIOR_ID";
const QString cDataManager::CONF_NOTIFICATION_CAT_SELECT_BEHAVIOR_ID = "NOTIFICATION_CAT_SELECT_BEHAVIOR";
const QString cDataManager::CONF_NOTIFICATION_VISIBILITY_BEHAVIOR_ID = "NOTIFICATION_VISIBILITY_BEHAVIOR";
const QString cDataManager::CONF_NOTIFICATION_HIDE_BORDERS_ID = "NOTIFICATION_HIDE_BORDERS";
const QString cDataManager::CONF_AUTORUN_ID = "AUTORUN_ENABLED";
const QString cDataManager::CONF_CLIENT_MODE_ID = "CLIENT_MODE";
const QString cDataManager::CONF_CLIENT_MODE_HOST_ID = "CLIENT_MODE_HOST";
const QString cDataManager::CONF_LAST_AVAILABLE_VERSION_ID = "LAST_AVAILABLE_VERSION";
const QString cDataManager::CONF_BACKUP_FILENAME_ID = "BACKUP_FILENAME";
const QString cDataManager::CONF_BACKUP_DELAY_ID = "BACKUP_DELAY";


cDataManager::cDataManager() :
  QObject(),
  m_ShowSystemNotifications(false),
  m_UpdateCounter(0),
  m_UpdateDelay(DEFAULT_SECONDS_UPDATE_DELAY),
  m_CurrentMousePos(QPoint(0,0)),
  m_CurrentApplicationIndex(-1),
  m_CurrentApplicationActivityIndex(-1),
  m_CurrentApplicationActivityCategory(-1),
  m_Idle(false),
  m_IdleCounter(0),
  m_IdleDelay(DEFAULT_SECONDS_IDLE_DELAY),
  m_AutoSaveCounter(0),
  m_AutoSaveDelay(DEFAULT_SECONDS_AUTOSAVE_DELAY),
  m_CurrentProfile(0)
{
#if (QT_VERSION < QT_VERSION_CHECK(5, 4, 0))
    m_StorageFileName = QStandardPaths::writableLocation(QStandardPaths::DataLocation)+"/db.bin";
#else
    m_StorageFileName = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)+"/db.bin";
#endif
    m_BackupFolder = QFileInfo(m_StorageFileName).absolutePath()+"/backup/";

    loadPreferences();
    loadDB();

    if (m_Profiles.empty()){
        sProfile defaultProfile;
        defaultProfile.name = tr("Default");
        m_Profiles.push_back(defaultProfile);
    }

    QObject::connect(&m_MainTimer, SIGNAL(timeout()), this, SLOT(process()));
    m_MainTimer.start(1000);
}

cDataManager::~cDataManager()
{    
    saveDB();
    for (auto app: m_Applications)
        delete app;
}

const sProfile *cDataManager::profiles(int index)
{
    if (index<0 || index>=m_Profiles.size())
        return nullptr;
    return &m_Profiles[index];
}

void cDataManager::addNewProfile(const QString &Name, int CloneProfileIndex)
{
    sProfile profile;
    profile.name = Name;
    m_Profiles.push_back(profile);

    const sActivityProfileState def_state = {-1, false};
    for (auto app: m_Applications)
        for (auto& act: app->activities) {
            act.categories.push_back(CloneProfileIndex == -1 ? def_state : act.categories[CloneProfileIndex]);
        }
    emit profilesChanged();
}

void cDataManager::mergeProfiles(int profile1, int profile2)
{
    int profileToDelete = profile2;
    int profileToSave = profile1;
    if (profileToSave>profileToDelete){
        profileToDelete = profile1;
        profileToSave = profile2;
    }

    m_Profiles.remove(profileToDelete);
    for (auto app: m_Applications)
        for (auto& act: app->activities) {
            act.categories.remove(profileToDelete);
            for (auto& period: act.periods) {
                if (period.profileIndex==profileToDelete) {
                    period.profileIndex = profileToSave;
                }
                else
                if (period.profileIndex>profileToDelete) {
                    period.profileIndex--;
                }
            }
        }
    if (profileToDelete==m_CurrentProfile)
        m_CurrentProfile = profileToSave;
    emit profilesChanged();
}

void cDataManager::addNewCategory(const QString &Name, QColor color)
{
    const sCategory cat = {Name, color};
    m_Categories.push_back(cat);
}

void cDataManager::deleteCategory(int index)
{
    for (auto app: m_Applications) {
        for (auto& act: app->activities) {
            for (auto& cat: act.categories) {
                if (cat.category == index)
                    cat.category = -1;
                else
                if (cat.category > index)
                    cat.category--;
            }
        }
    }
    m_Categories.remove(index);
    emit applicationsChanged();
}

void cDataManager::setApplicationActivityCategory(int profile, int appIndex, int activityIndex, int category)
{
    if (profile==-1) {
        for (auto& cat: m_Applications[appIndex]->activities[activityIndex].categories)
            cat.category = category;
    }
    else {
        m_Applications[appIndex]->activities[activityIndex].categories[profile].category = category;
    }

}

void cDataManager::makeBackup()
{
    int delayDays = -1;
    switch(m_BackupDelay){
        case BD_ONE_DAY:{
            delayDays = 1;
        }
        break;
        case BD_ONE_WEEK:{
            delayDays = 7;
        }
        break;
        case BD_ONE_MONTH:{
            delayDays = 31;
        }
        break;
        case BD_ONE_YEAR:{
            delayDays = 365;
        }
        break;
        case BD_FOREVER:{
            delayDays = -1;
        }
        break;
    }

    QDateTime now = QDateTime::currentDateTime();
    if (delayDays>-1){
        const QDir backFolder = m_BackupFolder;
        QStringList backupFiles = backFolder.entryList(QStringList() << "*.backup");
        for (const auto& bf: backupFiles){
            QFileInfo file(backFolder.filePath(bf));
            if (now.daysTo(file.lastModified())>=delayDays){
                QFile::remove(file.absoluteFilePath());
            }
        }
    }

    if (!m_StorageFileName.isEmpty())
        QFile::copy(m_StorageFileName,m_BackupFolder+"/"+QFileInfo(m_StorageFileName).baseName()+"."+now.toString("yyyy_MM_dd__HH_mm")+".backup");
}

void cDataManager::process()
{
    m_ExternalTrackers.update();

    m_UpdateCounter++;
    if (m_UpdateCounter<m_UpdateDelay)
        return;
    m_UpdateCounter = 0;

    bool isUserActive = getIdleTime()==0;

    //Update keyboard activity
    //if (isKeyboardChanged()){
    //    isUserActive = true;
   // }

    //Update mouse activity
    //QPoint mousePos = getMousePos();
    //if (m_CurrentMousePos!=mousePos){
    //    isUserActive = true;
    //    m_CurrentMousePos = mousePos;
    //}

    if (isUserActive)
        m_LastLocalActivity = 0;
    else
        m_LastLocalActivity+=m_UpdateDelay;
    int hostActivity = m_LastLocalActivity+1;
    sOverrideTrackerInfo* info = m_ExternalTrackers.getOverrideTracker();
    if (info)
        hostActivity = info->IdleTime-2;

    //Update application
    bool isAppChanged = false;
    sSysInfo currentAppInfo = getCurrentApplication();
    int appIndex = getAppIndex(currentAppInfo);
    int activityIndex = appIndex>-1?getActivityIndex(appIndex,currentAppInfo):0;

    if (m_LastLocalActivity > hostActivity) {
        if (info){
            const sSysInfo remoteInfo = {"", info->AppFileName, ""};
            appIndex = getAppIndex(remoteInfo);
            activityIndex = appIndex >-1 ? getActivityIndexDirect(appIndex,info->State) : 0;
            isUserActive = true;
        }
    }

    if (appIndex!=m_CurrentApplicationIndex || activityIndex!=m_CurrentApplicationActivityIndex){
        isUserActive = true;
        isAppChanged = true;
        m_CurrentApplicationIndex = appIndex;
        m_CurrentApplicationActivityIndex = activityIndex;
        if (m_CurrentApplicationIndex>-1){
            m_Applications[m_CurrentApplicationIndex]->activities[m_CurrentApplicationActivityIndex].categories[m_CurrentProfile].visible = true;
            int activityCategory = m_Applications[m_CurrentApplicationIndex]->activities[m_CurrentApplicationActivityIndex].categories[m_CurrentProfile].category;

            if (m_ShowSystemNotifications)
                if (m_CurrentApplicationActivityCategory != activityCategory || activityCategory==-1){
                    const QString hint = m_Profiles[m_CurrentProfile].name + ":" +
                      (m_CurrentApplicationActivityCategory == -1
                        ? tr("Uncategorized")
                        : m_Categories[m_CurrentApplicationActivityCategory].name);
                    emit trayShowHint(hint);
                }
            m_CurrentApplicationActivityCategory = activityCategory;
        }
    }
    emit showNotification();

    if (m_CurrentApplicationIndex>-1 && (!m_Idle || isAppChanged)){
        m_Applications[m_CurrentApplicationIndex]->activities[m_CurrentApplicationActivityIndex].incTime(isAppChanged,m_CurrentProfile,m_UpdateDelay);
        int category = m_Applications[m_CurrentApplicationIndex]->activities[m_CurrentApplicationActivityIndex].categories[m_CurrentProfile].category;
        emit statisticFastUpdate(m_CurrentApplicationIndex, m_CurrentApplicationActivityIndex, category, m_UpdateDelay, false);
    }

    if (isUserActive){
        m_IdleCounter = 0;
        if (m_Idle){
            emit trayActive();
            m_Idle = false;
        }
    }
    else{
        m_IdleCounter+=m_UpdateDelay;
        if (m_IdleCounter>m_IdleDelay && !m_Idle){
            emit traySleep();
            m_Idle = true;
            if (m_CurrentApplicationIndex>-1){
                m_Applications[m_CurrentApplicationIndex]->activities[m_CurrentApplicationActivityIndex].periods.last().length-=m_IdleCounter;
            }
            //force autosave
            m_AutoSaveCounter=m_AutoSaveDelay;
        }
    }

    if (!m_Idle)
        m_AutoSaveCounter+=m_UpdateDelay;

    if (m_AutoSaveCounter>=m_AutoSaveDelay){
        m_AutoSaveCounter = 0;
        saveDB();
    }

    if (!m_Idle && m_ClientMode){
      const int idleTime = m_IdleCounter == m_UpdateDelay ? 0 : m_IdleCounter;
      if (m_CurrentApplicationIndex > -1) {
            m_ExternalTrackers.sendOverrideTracker(
                m_Applications[m_CurrentApplicationIndex]->activities[0].name,
                m_Applications[m_CurrentApplicationIndex]->activities[m_CurrentApplicationActivityIndex].name,
                idleTime, m_ClientModeHost);
        }
        else{
            m_ExternalTrackers.sendOverrideTracker("", "", idleTime, m_ClientModeHost);
        }
    }
}

void cDataManager::onPreferencesChanged()
{
    saveDB(); //save to old storage
    loadPreferences(); //read new preferences
    loadDB();//reload current storage or load new if STORAGE_FILENAME changed
    emit profilesChanged();
}


int cDataManager::getAppIndex(const sSysInfo &FileInfo)
{
    if (FileInfo.fileName.isEmpty())
        return -1;

    QString upcaseFileName = FileInfo.fileName.toUpper();
    for (int i = 0; i<m_Applications.size(); i++){
        if (m_Applications[i]->activities[0].nameUpcase==upcaseFileName){
            if (m_Applications[i]->path.isEmpty() && !FileInfo.path.isEmpty()){
                m_Applications[i]->path = FileInfo.path;
                emit applicationsChanged();
            }
            return i;
        }
    }

    //app not exists in our list(first launch) - add to list
    sAppInfo* info = new sAppInfo(FileInfo.fileName,m_Profiles.size());
    info->path = FileInfo.path;

    m_Applications.push_back(info);
    emit applicationsChanged();

    return m_Applications.size()-1;
}

int cDataManager::getActivityIndex(int appIndex,const sSysInfo &FileInfo)
{    
    sAppInfo* appInfo = m_Applications[appIndex];

    QString activity;

    switch(appInfo->trackerType){
        case sAppInfo::eTrackerType::TT_EXECUTABLE_DETECTOR:
        case sAppInfo::eTrackerType::TT_EXTERNAL_DETECTOR:{
            if (!m_ExternalTrackers.getExternalTrackerState(appInfo->activities[0].nameUpcase,activity))
                activity="";
        };
            break;
        case sAppInfo::eTrackerType::TT_PREDEFINED_SCRIPT:{
            activity = m_ScriptsManager.getAppInfo(FileInfo,appInfo->predefinedInfo->script());
        };
            break;        
    }

    if (!m_DebugScript.isEmpty()){
        emit debugScriptResult(m_ScriptsManager.evaluteCustomScript(FileInfo,m_DebugScript,activity).toString(),FileInfo,activity);
    }

    if (appInfo->useCustomScript)
        activity = m_ScriptsManager.processCustomScript(FileInfo,appInfo->customScript,activity);

    return getActivityIndexDirect(appIndex,activity);
}

int cDataManager::getActivityIndexDirect(int appIndex, QString activityName)
{
    if (activityName.isEmpty())
        return 0;

    QString activityNameUpcase = activityName.toUpper();

    for (int i = 0; i<m_Applications[appIndex]->activities.size(); i++){
        if (m_Applications[appIndex]->activities[i].nameUpcase==activityNameUpcase){
            return i;
        }
    }

    sActivityInfo ainfo;
    ainfo.name = activityName;
    ainfo.nameUpcase = activityNameUpcase;
    ainfo.categories.resize(m_Profiles.size());
    for (int i = 0; i<ainfo.categories.size(); i++){
        ainfo.categories[i].category = -1;
        ainfo.categories[i].visible = false;
    }
    m_Applications[appIndex]->activities.push_back(ainfo);
    emit applicationsChanged();
    return m_Applications[appIndex]->activities.size()-1;
}

const int FILE_FORMAT_VERSION = 4;

void cDataManager::saveDB()
{
    if (m_StorageFileName.isEmpty())
        return;
    cFileBin file( m_StorageFileName+".new" );
    if ( file.open(QIODevice::WriteOnly) )
    {
        //header
        file.write(FILE_FORMAT_PREFIX,FILE_FORMAT_PREFIX_SIZE);
        file.writeInt(FILE_FORMAT_VERSION);

        //profiles
        file.writeInt(m_Profiles.size());
        for (int i = 0; i<m_Profiles.size(); i++){
            file.writeString(m_Profiles[i].name);
        }
        file.writeInt(m_CurrentProfile);

        //categories
        file.writeInt(m_Categories.size());
        for (int i = 0; i<m_Categories.size(); i++){
            file.writeString(m_Categories[i].name);
            file.writeUint(m_Categories[i].color.rgba());
        }

        //applications
        file.writeInt(m_Applications.size());
        for (int i = 0; i<m_Applications.size(); i++){
            file.writeInt(m_Applications[i]->visible?1:0);
            file.writeString(m_Applications[i]->path);
            file.writeInt(m_Applications[i]->trackerType);
            file.writeInt(m_Applications[i]->useCustomScript?1:0);
            file.writeString(m_Applications[i]->customScript);

            file.writeInt(m_Applications[i]->activities.size());
            for (int activity = 0; activity<m_Applications[i]->activities.size(); activity++){
                sActivityInfo* info = &m_Applications[i]->activities[activity];                
                file.writeString(info->name);

                //app category for every profile
                file.writeInt(info->categories.size());
                for (int j = 0; j<info->categories.size(); j++){
                    file.writeInt(info->categories[j].category);
                    file.writeInt(info->categories[j].visible?1:0);
                }

                //total use time
                file.writeInt(info->periods.size());
                for (int j = 0; j<info->periods.size(); j++){
                    file.writeUint(info->periods[j].start.toTime_t());
                    file.writeInt(info->periods[j].length);
                    file.writeInt(info->periods[j].profileIndex);
                }
            }
        }
        file.close();

        //if at any step of saving app fail proceed - old db will not damaged and can be restored
        QFile::rename(m_StorageFileName, m_StorageFileName+".old");
        QFile::rename(m_StorageFileName+".new", m_StorageFileName);
        QFile::remove(m_StorageFileName+".old");
    }
}

void cDataManager::loadDB()
{
    qDebug() << "cDataManager: store file " << m_StorageFileName;
    if (m_StorageFileName.isEmpty())
        return;
    if (!QFile(m_StorageFileName).exists())
        return;
    qDebug() << "cDataManager: start DB loading";
    for (int i = 0; i<m_Applications.size(); i++)
        delete m_Applications[i];
    m_Applications.resize(0);

    convertToVersion4(m_StorageFileName,m_StorageFileName);
    cFileBin file( m_StorageFileName );
    if ( file.open(QIODevice::ReadOnly) )
    {
        //check header
        char prefix[FILE_FORMAT_PREFIX_SIZE+1]; //add zero for simple convert to string
        prefix[FILE_FORMAT_PREFIX_SIZE] = 0;
        file.read(prefix,FILE_FORMAT_PREFIX_SIZE);
        if (memcmp(prefix,FILE_FORMAT_PREFIX,FILE_FORMAT_PREFIX_SIZE)==0){
            int Version = file.readInt();
            if (Version==FILE_FORMAT_VERSION){

                //profiles
                m_Profiles.resize(file.readInt());
                for (int i = 0; i<m_Profiles.size(); i++){
                    m_Profiles[i].name = file.readString();
                }
                m_CurrentProfile = file.readInt();

                //categories
                m_Categories.resize(file.readInt());
                for (int i = 0; i<m_Categories.size(); i++){
                    m_Categories[i].name = file.readString();
                    m_Categories[i].color = QColor::fromRgba(file.readUint());
                }

                //applications
                m_Applications.resize(file.readInt());
                for (int i = 0; i<m_Applications.size(); i++){
                    m_Applications[i] = new sAppInfo();
                    m_Applications[i]->visible = file.readInt()==1;
                    m_Applications[i]->path = file.readString();
                    m_Applications[i]->trackerType = static_cast<sAppInfo::eTrackerType>(file.readInt());
                    m_Applications[i]->useCustomScript = file.readInt()==1;
                    m_Applications[i]->customScript = file.readString();

                    m_Applications[i]->activities.resize(file.readInt());
                    for (int activity = 0; activity<m_Applications[i]->activities.size(); activity++){
                        sActivityInfo* info = &m_Applications[i]->activities[activity];                        
                        info->name = file.readString();
                        info->nameUpcase = info->name.toUpper();

                        //app category for every profile
                        info->categories.resize(file.readInt());
                        for (int j = 0; j<info->categories.size(); j++){
                            info->categories[j].category = file.readInt();
                            info->categories[j].visible = file.readInt()==1;
                        }

                        //total use time
                        info->periods.resize(file.readInt());
                        for (int j = 0; j<info->periods.size(); j++){
                            info->periods[j].start = QDateTime::fromTime_t(file.readUint());
                            info->periods[j].length = file.readInt();
                            info->periods[j].profileIndex = file.readInt();
                        }
                    }
                    m_Applications[i]->predefinedInfo = new cAppPredefinedInfo(m_Applications[i]->activities[0].name);
                }
            }
            else
                qCritical() << "Error loading db. Incorrect file format version " << Version << " only " << FILE_FORMAT_VERSION << " supported";
        }
        else
            qCritical() << "Error loading db. Incorrect file format prefix " << prefix;


        file.close();
    }
    qDebug() << "cDataManager: end DB loading\n";
}

void cDataManager::saveJSON()
{
    if (m_StorageFileName.isEmpty())
        return;
    QFile file(m_StorageFileName+".new" );
    if ( ! file.open(QIODevice::WriteOnly) )
        return;

    QJsonObject jobj;
    //header
    jobj["magic"] = FILE_FORMAT_PREFIX;
    jobj["version"] = FILE_FORMAT_VERSION;

    //profiles
    QJsonArray all_profiles;
    for (const auto& prof: m_Profiles)
      all_profiles.append(prof.name);

    QJsonObject profiles;
    profiles["current"] = m_CurrentProfile;
    profiles["list"] = all_profiles;
    jobj["profiles"] = profiles;

    //categories
    QJsonArray categories;
    for (const auto& cat: m_Categories) {
      QJsonObject jobj;
      jobj["name"] = cat.name;
      jobj["color"] = cat.color.name();
      categories.append(jobj);
    }
    jobj["categories"] = categories;

    //applications
    QJsonArray applications;
    for (const auto app: m_Applications) {

      QJsonObject jobj;
      jobj["visible"] = app->visible;
      jobj["path"] = app->path;
      jobj["trackerType"] = app->trackerType;
      jobj["useCustomScript"] = app->useCustomScript;
      jobj["customScript"] = app->customScript;

      QJsonArray activities;
      for (const auto& info: app->activities) {
        QJsonObject jobj;
        jobj["name"] = info.name;

        //app category for every profile
        QJsonArray categories;
        for (const auto& cat: info.categories) {
          QJsonObject jobj;
          jobj["category"] = cat.category;
          jobj["visible"] = cat.visible;
          categories.append(jobj);
        }
        jobj["categories"] = categories;

        //total use time
        QJsonArray periods;
        for (const auto& per: info.periods) {
          QJsonObject jobj;
          jobj["start"] = per.start.toString("yyyy-mm-dd hh:mm:ss");
          jobj["length"] = per.length;
          jobj["profileIndex"] = per.profileIndex;
          periods.append(jobj);
        }
        jobj["periods"] = periods;

        activities.append(jobj);
      }

      jobj["activities"] = activities;
    }
    jobj["applications"] = applications;

    QJsonDocument jdoc(jobj);
    file.write(jdoc.toJson());

    file.close();

    //if at any step of saving app fail proceed - old db will not damaged and can be restored
    QFile::rename(m_StorageFileName, m_StorageFileName+".old");
    QFile::rename(m_StorageFileName+".new", m_StorageFileName);
    QFile::remove(m_StorageFileName+".old");
}

void cDataManager::loadJSON()
{
    if (m_StorageFileName.isEmpty())
        return;
    if (!QFile(m_StorageFileName).exists())
        return;
    qDebug() << "cDataManager: start DB loading\n";

//    convertToVersion4(m_StorageFileName,m_StorageFileName);
    QFile file(m_StorageFileName);
    if ( !file.open(QIODevice::ReadOnly) )
      return;

    QJsonDocument jdoc = QJsonDocument::fromJson(file.readAll());
    file.close();

    QJsonObject jobj = jdoc.object();
    //check header
    const QString magic = jobj["magic"].toString();
    if (magic != FILE_FORMAT_PREFIX) {
        qCritical() << "Error loading db. Incorrect file format prefix " << magic;
    }
    const int version = jobj["version"].toInt();
    if (version != FILE_FORMAT_VERSION) {
        qCritical() << "Error loading db. Incorrect file format version " << version << " only " << FILE_FORMAT_VERSION << " supported";
        return;
    }

    for (auto app: m_Applications)
        delete app;
    m_Applications.resize(0);

    //profiles
    QJsonObject profiles = jobj["profiles"].toObject();
    m_CurrentProfile = profiles["current"].toInt();
    QJsonArray all_profiles = profiles["list"].toArray();
    m_Profiles.reserve(all_profiles.count());
    for (const auto prof: all_profiles)
        m_Profiles.append({prof.toString()});

    //categories
    QJsonArray categories = jobj["categories"].toArray();
    m_Categories.reserve(categories.count());
    for (const auto cat: categories) {
      QJsonObject jobj = cat.toObject();
      const QString name = jobj["name"].toString();
      const QColor color(jobj["color"].toString());
      m_Categories.append({name, color});
    }

    //applications
    QJsonArray applications = jobj["applications"].toArray();
    m_Applications.reserve(applications.count());
    for (const auto japp: applications) {
      QJsonObject jobj = japp.toObject();

      sAppInfo* app = new sAppInfo();
      m_Applications.append(app);
      app->visible = jobj["visible"].toBool();
      app->path = jobj["path"].toString();
      app->trackerType = static_cast<sAppInfo::eTrackerType>(jobj["trackerType"].toInt());
      app->useCustomScript = jobj["useCustomScript"].toBool();
      app->customScript = jobj["customScript"].toString();

      QJsonArray jactivities = jobj["activities"].toArray();
      auto& activities = app->activities;
      activities.reserve(jactivities.count());
      for (const auto jinfo: jactivities) {
        QJsonObject jobj = jinfo.toObject();

        const QString name = jobj["name"].toString();
        activities.append(sActivityInfo{});
        sActivityInfo& info = activities.last();
        info.name = name;
        info.nameUpcase = name.toUpper();

        //app category for every profile
        QJsonArray jcategories = jobj["categories"].toArray();
        auto& categories = info.categories;
        categories.reserve(jcategories.count());
        for (const auto jcat: jcategories) {
          QJsonObject jobj = jcat.toObject();
          int category = jobj["category"].toInt();
          bool visible = jobj["visible"].toBool();
          categories.append({category, visible});
        }

        //total use time
        QJsonArray jperiods = jobj["periods"].toArray();
        auto& periods = info.periods;
        periods.reserve(jperiods.count());
        for (const auto jper: jperiods) {
          QJsonObject jobj = jper.toObject();
          QDateTime start = QDateTime::fromString(jobj["start"].toString(), "yyyy-mm-dd hh:mm:ss");
          int length = jobj["length"].toInt();
          int profileIndex = jobj["profileIndex"].toInt();
          periods.append({start, length, profileIndex});
        }

      }
      app->predefinedInfo = new cAppPredefinedInfo(activities[0].name);
    }

    qDebug() << "cDataManager: end DB loading\n";
}

void cDataManager::loadPreferences()
{
    cSettings settings;

    m_UpdateDelay = settings.db()->value(CONF_UPDATE_DELAY_ID,m_UpdateDelay).toInt();
    m_IdleDelay = settings.db()->value(CONF_IDLE_DELAY_ID,m_IdleDelay).toInt();
    m_AutoSaveDelay = settings.db()->value(CONF_AUTOSAVE_DELAY_ID,m_AutoSaveDelay).toInt();
    m_StorageFileName = settings.db()->value(CONF_STORAGE_FILENAME_ID,m_StorageFileName).toString();
    m_ShowSystemNotifications = settings.db()->value(CONF_NOTIFICATION_SHOW_SYSTEM_ID,m_ShowSystemNotifications).toBool();
    m_ClientMode = settings.db()->value(CONF_CLIENT_MODE_ID,m_ClientMode).toBool();
    m_ClientModeHost = settings.db()->value(CONF_CLIENT_MODE_HOST_ID,m_ClientModeHost).toString();

    m_BackupDelay = static_cast<eBackupDelay>(settings.db()->value(CONF_BACKUP_DELAY_ID,BD_ONE_WEEK).toInt());
    m_BackupFolder = settings.db()->value(CONF_BACKUP_FILENAME_ID,m_BackupFolder).toString();

    if (!m_StorageFileName.isEmpty()){
        QDir storagePath(QFileInfo(m_StorageFileName).absolutePath());
        if (!storagePath.exists())
            storagePath.mkpath(".");
    }

    if (!m_BackupFolder.isEmpty()){
        QDir backupPath(m_BackupFolder);
        if (!backupPath.exists())
            backupPath.mkpath(".");
    }
}



void sActivityInfo::incTime(bool FirstTime, int CurrentProfile, int UpdateDelay)
{
    if (FirstTime){
        sTimePeriod period;
        period.start = QDateTime::currentDateTimeUtc();
        period.length = 0;
        period.profileIndex = CurrentProfile;
        periods.push_back(period);
    }
    periods.last().length+=UpdateDelay;
}

sAppInfo::sAppInfo(const QString& name, int profilesCount) :
  visible(true),
  useCustomScript(false),
  predefinedInfo(new cAppPredefinedInfo(name))
{

    sActivityInfo ainfo = {
        name, name.toUpper(), {}, {profilesCount, sActivityProfileState{-1, false}}};
//    ainfo.categories.resize(profilesCount);
//    for (int i = 0; i<ainfo.categories.size(); i++){
//        ainfo.categories[i].category = -1;
//        ainfo.categories[i].visible = false;
//    }
    activities.push_back(ainfo);

    trackerType = predefinedInfo->trackerType();
    customScript = predefinedInfo->script();
}

sAppInfo::sAppInfo() :
  visible(true),
  useCustomScript(false),
  predefinedInfo(nullptr)
{
}

sAppInfo::~sAppInfo()
{
    delete predefinedInfo;
}
