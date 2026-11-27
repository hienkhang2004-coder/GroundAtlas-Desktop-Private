#pragma once
#include <QImage>
#include <QString>
#include <QVector>

namespace ga {

// Zero-dependency Pure C++20 GIF89a animated image writer (LZW + adaptive 256-color palette + looping)
bool writeAnimatedGif(const QString &filePath, const QVector<QImage> &frames, int delayCentiseconds = 7);

} // namespace ga
