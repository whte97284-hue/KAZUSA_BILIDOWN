#pragma once

#include <QAbstractListModel>
#include <vector>
#include "../BiliTypes.hpp"

// 视频清晰度/编码格式模型 (供 QML 画质选择药丸列表 / 下拉框绑定)
class VideoTrackModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum TrackRoles {
        QualityRole = Qt::UserRole + 1,
        QualityDescRole,
        CodecIdRole,
        CodecNameRole,
        ResolutionRole,
        FpsRole,
        BandwidthRole,
        BandwidthDescRole,
        BaseUrlRole
    };

    explicit VideoTrackModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setTracks(const std::vector<VideoTrack> &tracks);
    void clear();

    const std::vector<VideoTrack>& getTracks() const { return m_tracks; }

private:
    std::vector<VideoTrack> m_tracks;
};

// 视频分 P / 剧集分集模型 (供 QML 选集抽屉 / 分P网格列表绑定)
class VideoPageModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum PageRoles {
        PageNumberRole = Qt::UserRole + 1,
        CidRole,
        TitleRole,
        DurationRole,
        DurationDescRole,
        FirstFrameRole
    };

    explicit VideoPageModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setPages(const std::vector<VideoPage> &pages);
    void clear();

    const std::vector<VideoPage>& getPages() const { return m_pages; }

private:
    std::vector<VideoPage> m_pages;
};
