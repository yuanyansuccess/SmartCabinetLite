#pragma once
// 智能柜Qt Widget 2.0  陌生人记录实体
// 映射表：stranger_log
#include <QString>
#include <QDateTime>

struct Stranger {
    int     strangerId   = 0;
    QString faceFeature;
    QString imagePath;
    QString device;
    QDateTime detectedAt;
    int     isReviewed   = 0;
};
