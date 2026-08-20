#include "BiliTaskModel.hpp"
#include <QFile>
#include <QJsonDocument>
#include <algorithm>

// ================= TaskModel =================

TaskModel::TaskModel(QObject *parent) : QAbstractListModel(parent) {}

int TaskModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid()) return 0;
    return static_cast<int>(m_tasks.size());
}

QVariant TaskModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(m_tasks.size())) {
        return QVariant();
    }

    const auto &t = m_tasks[index.row()];
    switch (role) {
        case TaskIdRole:           return static_cast<qint64>(t.id);
        case TitleRole:            return t.title;
        case CoverUrlRole:         return t.coverUrl;
        case OwnerNameRole:        return t.ownerName;
        case DurationDescRole:     return t.durationDesc;
        case VideoSizeTextRole:    return t.videoSizeText;
        case AudioSizeTextRole:    return t.audioSizeText;
        case QualityOptionsRole:   return t.qualityOptions;
        case AudioOptionsRole:     return t.audioOptions;
        case SelectedQualityIndexRole: return t.selectedQualityIndex;
        case SelectedAudioIndexRole:   return t.selectedAudioIndex;
        case StateRole:            return static_cast<int>(t.state);
        case ProgressRole:         return t.progress;
        case SpeedTextRole:        return t.speedText;
        case StatusTextRole:       return t.statusText;
        case FilePathRole:         return t.filePath;
        case Qt::DisplayRole:      return t.title;
        default:                   return QVariant();
    }
}

QHash<int, QByteArray> TaskModel::roleNames() const {
    QHash<int, QByteArray> roles;
    roles[TaskIdRole]           = "taskId";
    roles[TitleRole]            = "title";
    roles[CoverUrlRole]         = "coverUrl";
    roles[OwnerNameRole]        = "ownerName";
    roles[DurationDescRole]     = "durationDesc";
    roles[VideoSizeTextRole]    = "videoSizeText";
    roles[AudioSizeTextRole]    = "audioSizeText";
    roles[QualityOptionsRole]   = "qualityOptions";
    roles[AudioOptionsRole]     = "audioOptions";
    roles[SelectedQualityIndexRole] = "selectedQualityIndex";
    roles[SelectedAudioIndexRole]   = "selectedAudioIndex";
    roles[StateRole]            = "state";
    roles[ProgressRole]         = "progress";
    roles[SpeedTextRole]        = "speedText";
    roles[StatusTextRole]       = "statusText";
    roles[FilePathRole]         = "filePath";
    return roles;
}

int TaskModel::findRowById(int64_t id) const {
    for (size_t i = 0; i < m_tasks.size(); ++i) {
        if (m_tasks[i].id == id) return static_cast<int>(i);
    }
    return -1;
}

int64_t TaskModel::addTask(const DownloadTask &task) {
    DownloadTask t = task;
    t.id = m_nextId++;

    int row = static_cast<int>(m_tasks.size());
    beginInsertRows(QModelIndex(), row, row);
    m_tasks.push_back(t);
    endInsertRows();

    emit taskCountsChanged();
    return t.id;
}

bool TaskModel::taskById(int64_t id, DownloadTask &out) const {
    int row = findRowById(id);
    if (row < 0) return false;
    out = m_tasks[row];
    return true;
}

DownloadTask TaskModel::taskAtRow(int row) const {
    if (row < 0 || row >= static_cast<int>(m_tasks.size())) return DownloadTask();
    return m_tasks[row];
}

int64_t TaskModel::taskIdAtRow(int row) const {
    if (row < 0 || row >= static_cast<int>(m_tasks.size())) return 0;
    return m_tasks[row].id;
}

bool TaskModel::updateTask(int64_t id, const std::function<void(DownloadTask &)> &mutator) {
    int row = findRowById(id);
    if (row < 0) return false;

    mutator(m_tasks[row]);
    QModelIndex idx = index(row, 0);
    emit dataChanged(idx, idx);
    return true;
}

bool TaskModel::setState(int64_t id, DownloadState state) {
    int row = findRowById(id);
    if (row < 0) return false;
    if (m_tasks[row].state == state) return true;

    DownloadState oldState = m_tasks[row].state;
    m_tasks[row].state = state;

    QModelIndex idx = index(row, 0);
    emit dataChanged(idx, idx, { StateRole, StatusTextRole });
    emit taskStateChanged(id, static_cast<int>(oldState), static_cast<int>(state));
    emit taskCountsChanged();
    return true;
}

bool TaskModel::removeTask(int64_t id) {
    int row = findRowById(id);
    if (row < 0) return false;

    beginRemoveRows(QModelIndex(), row, row);
    m_tasks.erase(m_tasks.begin() + row);
    endRemoveRows();

    emit taskCountsChanged();
    return true;
}

void TaskModel::clearState(DownloadState state) {
    // 从后往前收集待删除行，保证删除时索引始终有效
    std::vector<int> rows;
    for (int r = static_cast<int>(m_tasks.size()) - 1; r >= 0; --r) {
        if (m_tasks[r].state == state) rows.push_back(r);
    }
    for (int r : rows) {
        beginRemoveRows(QModelIndex(), r, r);
        m_tasks.erase(m_tasks.begin() + r);
        endRemoveRows();
    }
    if (!rows.empty()) {
        emit taskCountsChanged();
        // 清空后释放冗余 capacity, 防长期使用向量容量只增不减
        if (m_tasks.empty()) m_tasks.shrink_to_fit();
    }
}

int TaskModel::stateCount(DownloadState state) const {
    return static_cast<int>(std::count_if(m_tasks.begin(), m_tasks.end(),
        [state](const DownloadTask &t) { return t.state == state; }));
}

std::vector<int64_t> TaskModel::taskIdsByState(DownloadState state) const {
    std::vector<int64_t> ids;
    for (const auto &t : m_tasks) {
        if (t.state == state) ids.push_back(t.id);
    }
    return ids;
}

// ================= 任务持久化支撑 =================

std::vector<DownloadTask> TaskModel::allTasks() const {
    return m_tasks;
}

int64_t TaskModel::nextId() const {
    return m_nextId;
}

void TaskModel::replaceAllTasks(const std::vector<DownloadTask> &tasks, int64_t nextId) {
    beginResetModel();
    m_tasks = tasks;
    if (m_tasks.empty()) m_tasks.shrink_to_fit();
    m_nextId = nextId > 0 ? nextId : 1;
    endResetModel();
    emit taskCountsChanged();
}

// ================= 任务 JSON 序列化 =================

QJsonObject downloadTaskToJson(const DownloadTask &t) {
    QJsonObject o;
    o["id"] = t.id;
    o["state"] = static_cast<int>(t.state);
    o["mode"] = static_cast<int>(t.mode);
    o["rawInput"] = t.rawInput;
    o["bvid"] = t.bvid;
    o["title"] = t.title;
    o["coverUrl"] = t.coverUrl;
    o["ownerName"] = t.ownerName;
    o["durationDesc"] = t.durationDesc;
    o["pageIndex"] = t.pageIndex;
    o["totalPages"] = t.totalPages;
    o["qualityOptions"] = QJsonArray::fromStringList(t.qualityOptions);
    o["audioOptions"] = QJsonArray::fromStringList(t.audioOptions);
    o["selectedQualityIndex"] = t.selectedQualityIndex;
    o["selectedAudioIndex"] = t.selectedAudioIndex;
    o["qualityId"] = t.qualityId;
    o["codecId"] = t.codecId;
    o["audioId"] = t.audioId;
    o["videoSizeText"] = t.videoSizeText;
    o["audioSizeText"] = t.audioSizeText;
    o["progress"] = t.progress;
    o["speedText"] = t.speedText;
    o["statusText"] = t.statusText;
    o["filePath"] = t.filePath;
    o["finishNote"] = t.finishNote;
    o["saveDir"] = t.saveDir;

    QJsonArray qIds, cIds, aIds;
    for (int v : t.qualityIds) qIds.append(v);
    for (int v : t.codecIds) cIds.append(v);
    for (int v : t.audioIds) aIds.append(v);
    o["qualityIds"] = qIds;
    o["codecIds"] = cIds;
    o["audioIds"] = aIds;
    return o;
}

DownloadTask downloadTaskFromJson(const QJsonObject &o) {
    DownloadTask t;
    t.id = o.value("id").toVariant().toLongLong();
    t.state = static_cast<DownloadState>(o.value("state").toInt());
    t.mode = static_cast<DownloadMode>(o.value("mode").toInt());
    t.rawInput = o.value("rawInput").toString();
    t.bvid = o.value("bvid").toString();
    t.title = o.value("title").toString();
    t.coverUrl = o.value("coverUrl").toString();
    t.ownerName = o.value("ownerName").toString();
    t.durationDesc = o.value("durationDesc").toString();
    t.pageIndex = o.value("pageIndex").toInt();
    t.totalPages = o.value("totalPages").toInt();
    t.qualityOptions = o.value("qualityOptions").toVariant().toStringList();
    t.audioOptions = o.value("audioOptions").toVariant().toStringList();
    t.selectedQualityIndex = o.value("selectedQualityIndex").toInt();
    t.selectedAudioIndex = o.value("selectedAudioIndex").toInt();
    t.qualityId = o.value("qualityId").toInt();
    t.codecId = o.value("codecId").toInt();
    t.audioId = o.value("audioId").toInt();
    t.videoSizeText = o.value("videoSizeText").toString();
    t.audioSizeText = o.value("audioSizeText").toString();
    t.progress = o.value("progress").toDouble();
    t.speedText = o.value("speedText").toString();
    t.statusText = o.value("statusText").toString();
    t.filePath = o.value("filePath").toString();
    t.finishNote = o.value("finishNote").toString();
    t.saveDir = o.value("saveDir").toString();

    const QJsonArray qIds = o.value("qualityIds").toArray();
    for (const QJsonValue &v : qIds) t.qualityIds.append(v.toInt());
    const QJsonArray cIds = o.value("codecIds").toArray();
    for (const QJsonValue &v : cIds) t.codecIds.append(v.toInt());
    const QJsonArray aIds = o.value("audioIds").toArray();
    for (const QJsonValue &v : aIds) t.audioIds.append(v.toInt());
    return t;
}

void saveTasksToFile(const QString &filePath, const std::vector<DownloadTask> &tasks, int64_t nextId) {
    QJsonArray arr;
    for (const auto &t : tasks) {
        arr.append(downloadTaskToJson(t));
    }
    QJsonObject root;
    root["version"] = 1;
    root["nextId"] = nextId;
    root["tasks"] = arr;

    QJsonDocument doc(root);
    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(doc.toJson(QJsonDocument::Indented));
        file.close();
    }
}

bool loadTasksFromFile(const QString &filePath, std::vector<DownloadTask> &tasks, int64_t &nextId) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    QByteArray data = file.readAll();
    file.close();

    QJsonParseError perr;
    QJsonDocument doc = QJsonDocument::fromJson(data, &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        return false;
    }

    QJsonObject root = doc.object();
    nextId = root.value("nextId").toVariant().toLongLong();
    if (nextId < 1) nextId = 1;

    tasks.clear();
    const QJsonArray arr = root.value("tasks").toArray();
    tasks.reserve(arr.size());
    for (const QJsonValue &v : arr) {
        if (!v.isObject()) continue;
        tasks.push_back(downloadTaskFromJson(v.toObject()));
    }
    return true;
}

// ================= TaskStateProxy =================

TaskStateProxy::TaskStateProxy(QObject *parent)
    : QSortFilterProxyModel(parent) {
    setDynamicSortFilter(true);
}

TaskStateProxy::TaskStateProxy(std::vector<DownloadState> states, QObject *parent)
    : QSortFilterProxyModel(parent), m_states(std::move(states)) {
    setDynamicSortFilter(true);
}

void TaskStateProxy::setStates(std::vector<DownloadState> states) {
    m_states = std::move(states);
    invalidateFilter();
}

bool TaskStateProxy::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const {
    QModelIndex idx = sourceModel()->index(sourceRow, 0, sourceParent);
    int state = idx.data(TaskModel::StateRole).toInt();
    for (DownloadState s : m_states) {
        if (static_cast<int>(s) == state) return true;
    }
    return false;
}
